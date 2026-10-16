#include "core/aura.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace aura {
namespace {
std::vector<std::string> parseCsv(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    bool quoted = false, closed = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else { quoted = false; closed = true; }
            } else field += c;
        } else if (c == ',') { result.push_back(field); field.clear(); closed = false; }
        else if (c == '"' && field.empty() && !closed) quoted = true;
        else if (closed || c == '"') throw std::runtime_error("Malformed CSV quoting");
        else field += c;
    }
    if (quoted) throw std::runtime_error("Unclosed CSV quote");
    result.push_back(field);
    return result;
}
std::uint64_t integer(const std::string& s) {
    std::uint64_t v{};
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) throw std::runtime_error("Invalid integer: " + s);
    return v;
}
float real(const std::string& s) {
    std::size_t end{};
    const float value = std::stof(s, &end);
    if (end != s.size() || !std::isfinite(value)) throw std::runtime_error("Invalid number: " + s);
    return value;
}
void validate(const std::vector<Definition>& definitions) {
    if (definitions.empty() || definitions.size() > 4096) throw std::runtime_error("Catalog requires 1..4096 auras");
    std::unordered_set<std::string> ids;
    unsigned fallback = 0;
    std::uint64_t total = 0;
    for (const auto& d : definitions) {
        if (d.id.empty() || d.id.size() > 64 || !std::all_of(d.id.begin(), d.id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }) || !ids.insert(d.id).second) throw std::runtime_error("Invalid or duplicate aura ID: " + d.id);
        if (d.name.empty() || d.name.size() > 128 || d.name.find_first_of("\r\n\t") != std::string::npos)
            throw std::runtime_error("Aura names must contain 1..128 UTF-8 bytes on one line");
        for (float c : {d.color.r, d.color.g, d.color.b})
            if (!std::isfinite(c) || c < 0 || c > 1) throw std::runtime_error("Colors must be in 0..1");
        if (d.particles > 160 || !std::isfinite(d.radius) || d.radius < .5f || d.radius > 3 ||
            !std::isfinite(d.speed) || d.speed < 0 || d.speed > 4) throw std::runtime_error("Effect parameters out of range");
        if (d.previewOnly) {
            if (d.denominator != 0) throw std::runtime_error("Preview-only auras must have denominator=0");
        }
        else if (d.denominator == 0) ++fallback;
        else {
            if (d.denominator < 2 || d.denominator > TicketPool) throw std::runtime_error("Denominator must be 2..10^12");
            total += TicketPool / d.denominator;
            if (total >= TicketPool) throw std::runtime_error("Aura probabilities leave no fallback probability");
        }
    }
    if (fallback != 1) throw std::runtime_error("Exactly one denominator=0 fallback is required");
}
std::uint64_t collectedTotal(const Progress& progress) {
    std::uint64_t total=0;
    const auto add=[&](std::uint64_t count) {
        if(count>std::numeric_limits<std::uint64_t>::max()-total) throw std::overflow_error("Inventory total overflow");
        total+=count;
    };
    for(auto count:progress.counts) add(count);
    for(const auto& entry:progress.retiredCounts) add(entry.second);
    return total;
}
} // namespace

std::vector<Definition> loadCatalog(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open aura catalog: " + path.string());
    std::string line;
    std::getline(in, line);
    if (line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    constexpr auto header = "id,name,denominator,red,green,blue,particles,radius,speed";
    const bool hasPreview = line == std::string(header) + ",preview_only";
    if (line != header && !hasPreview) throw std::runtime_error("Unexpected catalog header");
    std::vector<Definition> result;
    unsigned number = 1;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.size() > 2048) throw std::runtime_error("Catalog row too long");
        try {
            auto f = parseCsv(line);
            if (f.size() != (hasPreview ? 10u : 9u)) throw std::runtime_error("Catalog column count does not match header");
            const auto preview = hasPreview ? integer(f[9]) : 0;
            if (preview > 1) throw std::runtime_error("preview_only must be 0 or 1");
            const auto particles = integer(f[6]);
            if (particles > 160) throw std::runtime_error("Maximum particle count is 160");
            result.push_back({f[0], f[1], integer(f[2]), {real(f[3]), real(f[4]), real(f[5])},
                static_cast<unsigned>(particles), real(f[7]), real(f[8]), preview != 0});
        } catch (const std::exception& e) { throw std::runtime_error("Catalog row " + std::to_string(number) + ": " + e.what()); }
    }
    validate(result);
    return result;
}

Roller::Roller(std::vector<Definition> definitions, std::uint64_t seed)
    : definitions_(std::move(definitions)), random_(seed) {
    validate(definitions_);
    weights_.resize(definitions_.size());
    std::uint64_t remaining = TicketPool;
    std::size_t fallback = 0;
    for (std::size_t i = 0; i < definitions_.size(); ++i) {
        if (definitions_[i].previewOnly) continue;
        if (definitions_[i].denominator == 0) fallback = i;
        else { weights_[i] = TicketPool / definitions_[i].denominator; remaining -= weights_[i]; }
    }
    weights_[fallback] = remaining;
}
std::size_t Roller::selectTicket(std::uint64_t ticket) const {
    if (ticket >= TicketPool) throw std::out_of_range("Ticket is outside pool");
    for (std::size_t i = 0; i < weights_.size(); ++i) {
        if (ticket < weights_[i]) return i;
        ticket -= weights_[i];
    }
    throw std::logic_error("Invalid ticket distribution");
}
std::size_t Roller::roll() {
    return selectTicket(std::uniform_int_distribution<std::uint64_t>(0, TicketPool - 1)(random_));
}
std::uint64_t Roller::tickets(std::size_t index) const { return weights_.at(index); }
void Roller::setLuck(double value) {
    if (!std::isfinite(value) || value < 1 || value > 1'000'000) throw std::out_of_range("Luck must be 1..1000000");
    // The rarest result of L independent baseline rolls, evaluated from the CDF.
    // This also supports fractional L without a loop or a probability overflow.
    std::vector<std::size_t> order;
    for (std::size_t i=0;i<definitions_.size();++i) if (!definitions_[i].previewOnly) order.push_back(i);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return probabilityTickets(definitions_,a)>probabilityTickets(definitions_,b);});
    std::uint64_t tail=TicketPool, previous=0;
    for (auto i:order) {
        tail-=probabilityTickets(definitions_,i);
        const auto cumulative=value==1?TicketPool-tail:static_cast<std::uint64_t>(std::clamp(
            std::round(std::exp(value*std::log1p(-static_cast<double>(tail)/TicketPool))*TicketPool),0.,static_cast<double>(TicketPool)));
        weights_[i]=cumulative-previous; previous=cumulative;
    }
    luck_=value;
}
std::uint64_t probabilityTickets(const std::vector<Definition>& catalog, std::size_t index) {
    if (catalog.at(index).previewOnly) return 0;
    const auto denominator = catalog.at(index).denominator;
    if (denominator) return TicketPool / denominator;
    std::uint64_t remaining = TicketPool;
    for (const auto& d : catalog) if (!d.previewOnly && d.denominator) remaining -= TicketPool / d.denominator;
    return remaining;
}
std::string oddsLabel(const std::vector<Definition>& catalog, std::size_t index) {
    const auto& d = catalog.at(index);
    if (d.previewOnly) return "Preview only";
    if (d.denominator) return "1 / " + formatNumber(d.denominator);
    std::ostringstream out;
    out << "~1 / " << std::fixed << std::setprecision(2)
        << static_cast<double>(TicketPool) / static_cast<double>(probabilityTickets(catalog, index));
    return out.str();
}
void Progress::record(std::size_t index, const std::vector<Definition>& catalog) {
    if (counts.size() != catalog.size() || index >= catalog.size()) throw std::out_of_range("Progress catalog mismatch");
    if (catalog[index].previewOnly) throw std::invalid_argument("Cannot record a preview-only aura as a roll");
    if (rolls == std::numeric_limits<std::uint64_t>::max() || counts[index] == std::numeric_limits<std::uint64_t>::max() ||
        collectedTotal(*this)==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Roll counter overflow");
    ++rolls; ++counts[index];
    if (counts.at(best)==0 || probabilityTickets(catalog, index) < probabilityTickets(catalog, best)) best = index;
    // Rolling changes inventory and statistics only. Collection owns equipment selection.
}
std::string oddsInLabel(const std::vector<Definition>& catalog,std::size_t index) {
    auto label=oddsLabel(catalog,index);
    const auto slash=label.find(" / ");
    if(slash!=std::string::npos) label.replace(slash,3," in ");
    return label;
}
unsigned Progress::auraLevel(std::size_t index,const std::vector<Definition>& catalog) const {
    const auto& id=catalog.at(index).id;
    if((id!="singularity" && id!="pixelation") || counts.at(index)==0) return 0;
    return static_cast<unsigned>(std::min<std::uint64_t>(3,counts.at(index)-1));
}
std::size_t Progress::discovered() const {
    return static_cast<std::size_t>(std::count_if(counts.begin(), counts.end(), [](auto n) { return n != 0; }));
}
void saveProgress(const std::filesystem::path& path, const Progress& p, const std::vector<Definition>& catalog) {
    if (p.counts.size() != catalog.size() || p.equipped >= catalog.size() || p.best >= catalog.size())
        throw std::runtime_error("Invalid progress for save");
    if (catalog[p.equipped].previewOnly || catalog[p.best].previewOnly)
        throw std::runtime_error("Preview-only auras cannot be equipment or discoveries");
    for (std::size_t i=0;i<catalog.size();++i) if (catalog[i].previewOnly && p.counts[i]!=0)
        throw std::runtime_error("Preview-only auras cannot have inventory counts");
    const auto collected=collectedTotal(p);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "AURA_RNG_SAVE 2\nrolls " << p.rolls << "\ncollected "<<collected<<"\nequipped " << catalog[p.equipped].id << '\n';
    for (std::size_t i = 0; i < catalog.size(); ++i) out << "count " << catalog[i].id << ' ' << p.counts[i] << '\n';
    for (const auto& [id, count] : p.retiredCounts) out << "count " << id << ' ' << count << '\n';
    out.flush();
    if (!out) throw std::runtime_error("Cannot write progress save");
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace progress save");
#else
    std::filesystem::rename(temporary, path);
#endif
}
Progress loadProgress(const std::filesystem::path& path, const std::vector<Definition>& catalog) {
    Progress p(catalog.size());
    const auto initial=std::find_if(catalog.begin(),catalog.end(),[](const auto& d){return !d.previewOnly;});
    if (initial==catalog.end()) throw std::runtime_error("Catalog has no rollable auras");
    p.equipped=p.best=static_cast<std::size_t>(initial-catalog.begin());
    for (std::size_t i = 0; i < catalog.size(); ++i)
        if (catalog[i].id == "still" && !catalog[i].previewOnly) p.equipped = p.best = i;
    if (!std::filesystem::exists(path)) return p;
    if (std::filesystem::file_size(path) > 1024 * 1024) throw std::runtime_error("Save file too large");
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    const bool legacy=line=="AURA_RNG_SAVE 1";
    if (!legacy && line != "AURA_RNG_SAVE 2") throw std::runtime_error("Unsupported progress save");
    std::unordered_set<std::string> seen;
    bool gotRolls = false, gotEquipped = false, gotCollected=false;
    std::uint64_t total = 0, declaredCollected=0;
    std::string equipped;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string kind, id, value, extra;
        row >> kind;
        if (kind == "rolls" && !gotRolls) { row >> value; p.rolls = integer(value); gotRolls = true; }
        else if (kind == "collected" && !legacy && !gotCollected) {row>>value; declaredCollected=integer(value); gotCollected=true;}
        else if (kind == "equipped" && !gotEquipped) { row >> equipped; gotEquipped = true; }
        else if (kind == "count") {
            row >> id >> value;
            if (id.empty() || !seen.insert(id).second) throw std::runtime_error("Duplicate save aura");
            const auto count = integer(value);
            if (count > std::numeric_limits<std::uint64_t>::max() - total) throw std::runtime_error("Save count overflow");
            total += count;
            bool found = false;
            for (std::size_t i = 0; i < catalog.size(); ++i) if (catalog[i].id == id) {
                if (catalog[i].previewOnly && count!=0) throw std::runtime_error("Preview-only aura in progress inventory");
                p.counts[i] = count; found = true;
            }
            if (!found) p.retiredCounts.emplace_back(id, count);
        } else throw std::runtime_error("Invalid progress save row");
        if (row >> extra) throw std::runtime_error("Unexpected save columns");
    }
    if (!gotRolls || !gotEquipped || (legacy?total!=p.rolls:(!gotCollected || total!=declaredCollected)))
        throw std::runtime_error("Inconsistent progress save");
    bool foundAny = false;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (catalog[i].previewOnly) continue;
        if (p.counts[i] && (!foundAny || probabilityTickets(catalog, i) < probabilityTickets(catalog, p.best))) p.best = i;
        if (p.counts[i]) foundAny = true;
        if (catalog[i].id == equipped && p.counts[i]) p.equipped = i;
    }
    return p;
}
std::string csvQuote(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
void appendLog(const std::filesystem::path& path, const LogEntry& entry, const Definition& d) {
    if (d.previewOnly) throw std::invalid_argument("Cannot log a preview-only aura as a roll");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const bool header = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (header) out << "roll,utc_time,aura_id,aura_name,base_denominator\n";
    out << entry.roll << ',' << csvQuote(entry.time) << ',' << csvQuote(d.id) << ',' << csvQuote(d.name) << ',' << d.denominator << '\n';
    out.flush();
    if (!out) throw std::runtime_error("Cannot append roll log");
}
std::string utcNow() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out; out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ"); return out.str();
}
std::string formatNumber(std::uint64_t value) {
    std::string s = std::to_string(value);
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}
} // namespace aura
