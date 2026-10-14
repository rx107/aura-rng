#pragma once
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <utility>

namespace aura {
inline constexpr std::uint64_t TicketPool = 1'000'000'000'000ULL;
struct Color { float r{}, g{}, b{}; };
struct Definition {
    std::string id, name;
    std::uint64_t denominator{}; // Zero denotes the single fallback aura.
    Color color;
    unsigned particles{};
    float radius{}, speed{};
};
std::vector<Definition> loadCatalog(const std::filesystem::path& path);
std::string csvQuote(const std::string& value);
std::string formatNumber(std::uint64_t value);

class Roller {
public:
    explicit Roller(std::vector<Definition> definitions, std::uint64_t seed);
    std::size_t roll();
    std::size_t selectTicket(std::uint64_t ticket) const;
    std::uint64_t tickets(std::size_t index) const;
    const std::vector<Definition>& catalog() const { return definitions_; }
private:
    std::vector<Definition> definitions_;
    std::vector<std::uint64_t> weights_;
    std::mt19937_64 random_;
};

struct Progress {
    std::uint64_t rolls{};
    std::vector<std::uint64_t> counts;
    std::vector<std::pair<std::string, std::uint64_t>> retiredCounts;
    std::size_t equipped{}, best{};
    explicit Progress(std::size_t count) : counts(count, 0) {}
    void record(std::size_t index, const std::vector<Definition>& catalog);
    std::size_t discovered() const;
};
void saveProgress(const std::filesystem::path& path, const Progress& progress,
                  const std::vector<Definition>& catalog);
Progress loadProgress(const std::filesystem::path& path, const std::vector<Definition>& catalog);
struct LogEntry { std::uint64_t roll{}; std::string time; std::size_t aura{}; };
void appendLog(const std::filesystem::path& path, const LogEntry& entry, const Definition& definition);
std::string utcNow();
} // namespace aura
