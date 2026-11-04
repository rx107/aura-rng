#include "render/renderer.hpp"
#include "core/aura_selector.hpp"
#include <algorithm>
#include <array>

namespace aura {
namespace {
const auto White = D2D1::ColorF(1.f, 1.f, 1.f);
const auto Muted = D2D1::ColorF(.85f, .87f, .89f);
const auto Accent = D2D1::ColorF(.55f, .96f, .82f);
const auto FrameLine = D2D1::ColorF(1.f, 1.f, 1.f, .26f);
const auto RowBase = D2D1::ColorF(.08f, .08f, .09f, .36f);
const auto RowHover = D2D1::ColorF(.19f, .20f, .22f, .70f);
const auto RowSelected = D2D1::ColorF(.15f, .29f, .26f, .92f);
const auto Card = D2D1::ColorF(tuning::aura_selector::PanelGray, tuning::aura_selector::PanelGray,
    tuning::aura_selector::PanelGray, tuning::aura_selector::PanelOpacity);

// ステージ限定のオーラには、そのステージでの基礎確率を表示します。
Biome oddsBiome(AuraCategory category, Biome current) {
    if (category == AuraCategory::Desert) return Biome::Desert;
    if (category == AuraCategory::Extra) return Biome::Extra;
    return current;
}
}

void Renderer::drawAuraButton(const Frame& frame) {
    const auto button = shell::auraButtonLayout(logicalW_, logicalH_);
    const bool hovered = button.contains(frame.mouseX / scale_, frame.mouseY / scale_);
    const auto fill = frame.auraSelectorOpen ? RowSelected : (hovered ? RowHover : Card);
    rect(button.x, button.y, button.width, button.height, fill);
    stroke(button.x, button.y, button.width, button.height, frame.auraSelectorOpen || hovered ? Accent : FrameLine);

    // 輪郭と内側の抜きに曲線を使い、拡大しても白い炎が滑らかに見えます。
    if (!flameGeometry_) {
        checked(d2dFactory_->CreatePathGeometry(&flameGeometry_), "Aura flame geometry");
        ComPtr<ID2D1GeometrySink> sink;
        checked(flameGeometry_->Open(&sink), "Aura flame sink");
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        auto curve = [&](D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F end) {
            sink->AddBezier(D2D1::BezierSegment(a, b, end));
        };
        sink->BeginFigure({15.f, 0.f}, D2D1_FIGURE_BEGIN_FILLED);
        curve({17.f, 5.f}, {24.f, 9.f}, {22.f, 15.f});
        curve({25.f, 14.f}, {26.f, 11.f}, {25.f, 9.f});
        curve({29.f, 13.f}, {32.f, 19.f}, {31.f, 24.f});
        curve({30.f, 31.f}, {24.f, 35.f}, {16.f, 35.f});
        curve({8.f, 35.f}, {2.f, 31.f}, {1.f, 24.f});
        curve({0.f, 20.f}, {2.f, 14.f}, {7.f, 10.f});
        curve({6.f, 15.f}, {8.f, 17.f}, {10.f, 18.f});
        curve({9.f, 11.f}, {15.f, 8.f}, {15.f, 0.f});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure({18.f, 18.f}, D2D1_FIGURE_BEGIN_FILLED);
        curve({18.f, 23.f}, {24.f, 24.f}, {23.f, 29.f});
        curve({22.f, 33.f}, {18.f, 34.f}, {15.f, 32.f});
        curve({11.f, 30.f}, {11.f, 26.f}, {13.f, 23.f});
        curve({14.f, 25.f}, {15.f, 26.f}, {15.f, 25.f});
        curve({15.f, 22.f}, {17.f, 21.f}, {18.f, 18.f});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        checked(sink->Close(), "Close aura flame geometry");
    }
    D2D1_MATRIX_3X2_F transform;
    d2d_->GetTransform(&transform);
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(
        button.x + (button.width - 32.f) * .5f, button.y + (button.height - 35.f) * .5f) * transform);
    brush_->SetColor(White);
    d2d_->FillGeometry(flameGeometry_.Get(), brush_.Get());
    d2d_->SetTransform(transform);
}

void Renderer::drawAuraSelector(const Frame& frame) {
    // 一覧、詳細、クリック判定は同じレイアウトを使います。
    const auto layout = selector::layout(logicalW_, logicalH_);
    const auto& panel = layout.panel;
    const auto& defs = frame.roller.catalog();
    const auto language = frame.presentation.settings.language;
    const auto t = [&](const char* en, const char* ja) { return tr(language, en, ja); };
    const auto hover = [&](const shell::UiRect& area) {
        return area.contains(frame.mouseX / scale_, frame.mouseY / scale_);
    };
    const auto paint = [&](const shell::UiRect& area, D2D1_COLOR_F color) {
        rect(area.x, area.y, area.width, area.height, color);
    };
    const auto outline = [&](const shell::UiRect& area, D2D1_COLOR_F color) {
        stroke(area.x, area.y, area.width, area.height, color);
    };
    paint(panel, Card);
    outline(panel, FrameLine);
    label(t("Auras", "オーラ"), panel.x + 24.f, panel.y + 15.f, panel.width - 96.f, 51.f, 35.f, White, true);
    label(t("Scroll the list and select an aura", "一覧をスクロールしてオーラを選択"),
        panel.x + 26.f, layout.list.y - 31.f, panel.width - 172.f, 23.f, 15.f, Muted);

    const auto& close = layout.close;
    paint(close, hover(close) ? RowHover : RowBase);
    outline(close, FrameLine);
    const float closeX = close.x + close.width * .5f;
    const float closeY = close.y + close.height * .5f;
    brush_->SetColor(White);
    d2d_->DrawLine({closeX - 5.f, closeY - 5.f}, {closeX + 5.f, closeY + 5.f}, brush_.Get(), 1.7f, roundStroke_.Get());
    d2d_->DrawLine({closeX + 5.f, closeY - 5.f}, {closeX - 5.f, closeY + 5.f}, brush_.Get(), 1.7f, roundStroke_.Get());

    const auto items = collectionItems(frame);
    const auto first = static_cast<std::size_t>(std::clamp(frame.collectionScroll, 0, selector::maxScroll(layout, items.size())));
    const auto end = std::min(items.size(), first + static_cast<std::size_t>(selector::visibleRows(layout)));
    if (items.empty()) {
        label(t("No auras in this category", "このカテゴリにオーラはありません"),
            layout.list.x + 12.f, layout.list.y + 14.f, layout.list.width - 24.f, 30.f, 17.f, Muted);
    } else {
        const auto range = std::to_string(first + 1) + "–" + std::to_string(end) + " / " + std::to_string(items.size());
        label(range, panel.x + panel.width - 129.f, layout.list.y - 31.f, 103.f, 23.f, 15.f, Muted, false, true);
    }

    // 選択中の行は帯と枠で区別し、オーラ自体の色は左の星に使います。
    for (auto i = first; i < end; ++i) {
        const auto index = items[i];
        const auto& definition = defs[index];
        const auto row = selector::row(layout, i - first);
        const bool selected = frame.selectedAura == index;
        const bool owned = index < frame.progress.counts.size() && frame.progress.counts[index] > 0;
        const bool equipped = owned && frame.progress.equipped == index;
        paint(row, selected ? RowSelected : (hover(row) ? RowHover : RowBase));
        if (selected) {
            outline(row, Accent);
            rect(row.x, row.y, 4.f, row.height, Accent);
        }
        const float centerY = row.y + row.height * .5f;
        rect(row.x + 14.f, centerY - 18.f, 36.f, 36.f, RowBase);
        star(row.x + 32.f, centerY, 12.f, D2D1::ColorF(definition.color.r, definition.color.g, definition.color.b));
        label(displayName(definition, language), row.x + 64.f, centerY - 16.f, row.width - 193.f, 33.f, 22.f, White, selected);
        const auto status = definition.previewOnly ? t("Preview only", "プレビュー専用") :
            equipped ? t("Equipped", "装備中") : owned ? t("Owned", "所持") : t("Locked", "未所持");
        label(status, row.x + row.width - 117.f, centerY - 11.f, 101.f, 24.f, 14.f, equipped ? Accent : Muted, equipped, true);
    }

    if (selector::maxScroll(layout, items.size()) > 0) {
        paint(layout.scrollbar, D2D1::ColorF(.10f, .10f, .10f, .56f));
        paint(selector::thumb(layout, items.size(), static_cast<int>(first)), D2D1::ColorF(.83f, .87f, .88f, .86f));
    }

    const std::array<const char*, 4> categories = {t("Normal", "通常"), t("Desert", "砂漠"),
        t("Extra", "エクストラ"), t("Administrator", "管理者")};
    const auto firstTab = selector::tab(layout, 0);
    label(t("CATEGORY", "カテゴリ"), firstTab.x, firstTab.y - 22.f, 130.f, 19.f, 12.f, Muted, true);
    for (std::size_t i = 0; i < categories.size(); ++i) {
        const auto tab = selector::tab(layout, i);
        const bool active = static_cast<std::size_t>(frame.collectionCategory) == i;
        paint(tab, active ? RowSelected : (hover(tab) ? RowHover : RowBase));
        outline(tab, active ? Accent : FrameLine);
        if (active) rect(tab.x, tab.y + tab.height - 3.f, tab.width, 3.f, Accent);
        label(categories[i], tab.x + 4.f, tab.y + (tab.height - 24.f) * .5f, tab.width - 8.f, 24.f, 16.f,
            active ? Accent : White, active, true);
    }

    if (!frame.selectedAura || *frame.selectedAura >= defs.size()) return;
    const auto index = *frame.selectedAura;
    const auto& definition = defs[index];
    const auto& details = layout.details;
    const auto& equip = layout.equip;
    const auto odds = definition.previewOnly ? std::string(t("Preview only", "プレビュー専用")) :
        oddsInLabel(defs, index, oddsBiome(auraCategory(definition), frame.presentation.settings.biome));
    const bool owned = index < frame.progress.counts.size() && frame.progress.counts[index] > 0;
    const bool equipped = owned && frame.progress.equipped == index && !frame.presentation.preview;
    const bool canEquip = canEquipAura(frame, index);

    paint(details, Card);
    outline(details, FrameLine);
    rect(details.x, details.y, 4.f, details.height, D2D1::ColorF(definition.color.r, definition.color.g, definition.color.b));
    label(t("AURA DETAILS", "オーラ詳細"), details.x + 23.f, details.y + 16.f, details.width - 46.f, 23.f, 14.f, Muted, true);
    label(displayName(definition, language), details.x + 23.f, details.y + 44.f, details.width - 46.f, 43.f, 31.f, White, true);
    label(odds, details.x + 24.f, details.y + 88.f, details.width - 48.f, 30.f, 21.f, Accent, true);
    paint(equip, canEquip ? (hover(equip) ? D2D1::ColorF(.72f, 1.f, .91f) : Accent) : RowBase);
    outline(equip, canEquip ? Accent : FrameLine);
    const auto action = equipped ? t("Equipped", "装備中") : canEquip ? t("Equip", "装備する") :
        definition.previewOnly ? t("Preview only", "プレビュー専用") : t("Not owned", "未所持");
    label(action, equip.x + 4.f, equip.y + (equip.height - 25.f) * .5f, equip.width - 8.f, 25.f, 17.f,
        canEquip ? D2D1::ColorF(.05f, .12f, .10f) : Muted, true, true);
}
}
