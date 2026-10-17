#pragma once
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <utility>

namespace aura {
// 抽選の仕組み。このファイルは画面やDirectXを使わず、データだけを扱います。
// オーラの名前・確率・標準の色を変えるだけなら data/auras.csv を編集してください。
inline constexpr std::uint64_t TicketPool = 1'000'000'000'000ULL;
struct Color { float r{}, g{}, b{}; };
struct Definition {
    std::string id; // セーブで識別する不変の名前。既存idを変えると別オーラとして扱われる。
    std::string name; // 画面上の英語名。日本語訳はpresentation.cppでidに対応付ける。
    std::uint64_t denominator{}; // 例: 4なら1/4。0は残余確率、または未抽選の試作。
    Color color;
    unsigned particles{}; // 標準オーラの粒子数。特殊オーラの数は専用tuning/*.hppで決める。
    float radius{}; // 標準の輪の半径 [ワールド単位]。
    float speed{}; // 標準の輪/粒子の回転速度係数。
    bool previewOnly{}; // trueならF1からの表示専用。ロール・在庫・履歴へ登録しない。
};
std::vector<Definition> loadCatalog(const std::filesystem::path& path);
std::string csvQuote(const std::string& value);
std::string formatNumber(std::uint64_t value);
std::uint64_t probabilityTickets(const std::vector<Definition>& catalog, std::size_t index);
std::string oddsLabel(const std::vector<Definition>& catalog, std::size_t index);
std::string oddsInLabel(const std::vector<Definition>& catalog, std::size_t index);

class Roller {
public:
    explicit Roller(std::vector<Definition> definitions, std::uint64_t seed);
    std::size_t roll(); // 抽選結果の「カタログ内の番号」を返す。所持数の更新はProgress担当。
    std::size_t selectTicket(std::uint64_t ticket) const;
    std::uint64_t tickets(std::size_t index) const;
    void setLuck(double value);
    double luck() const { return luck_; }
    const std::vector<Definition>& catalog() const { return definitions_; }
private:
    std::vector<Definition> definitions_;
    std::vector<std::uint64_t> weights_;
    std::mt19937_64 random_;
    double luck_{1};
};

struct Progress {
    std::uint64_t rolls{}; // 総ロール回数。F1で変更可能なカウンター。
    std::vector<std::uint64_t> counts; // counts[i]がカタログi番目の所持数。
    std::vector<std::pair<std::string, std::uint64_t>> retiredCounts; // CSVから一時的に削除したidの所持数も保持。
    std::size_t equipped{}, best{}; // equippedは装備番号、bestは一番珍しい発見の番号。
    explicit Progress(std::size_t count) : counts(count, 0) {}
    void record(std::size_t index, const std::vector<Definition>& catalog); // 在庫と統計だけ更新。勝手に装備しない。
    void setRollCount(std::uint64_t value) { rolls=value; }
    std::size_t discovered() const;
    unsigned auraLevel(std::size_t index,const std::vector<Definition>& catalog) const;
};
void saveProgress(const std::filesystem::path& path, const Progress& progress,
                  const std::vector<Definition>& catalog);
Progress loadProgress(const std::filesystem::path& path, const std::vector<Definition>& catalog);
struct LogEntry { std::uint64_t roll{}; std::string time; std::size_t aura{}; };
void appendLog(const std::filesystem::path& path, const LogEntry& entry, const Definition& definition);
std::string utcNow();
} // namespace aura
