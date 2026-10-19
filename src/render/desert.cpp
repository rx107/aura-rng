#include "render/renderer.hpp"
#include "tuning/desert.hpp"
#include <cmath>

namespace aura {
using namespace DirectX;
namespace {
constexpr float Pi = 3.14159265359f;
namespace settings = tuning::desert;

// 毎回同じ番号から同じ値を作り、砂粒がフレームごとに飛び散らないようにします。
float random(unsigned index, unsigned salt) {
    const float value = std::sin(index * 127.1f + salt * 311.7f) * 43758.5453f;
    return value - std::floor(value);
}
Color color(const std::array<float, 3>& rgb) { return {rgb[0], rgb[1], rgb[2]}; }
}

void Renderer::buildDesert(const Frame& f) {
    // この関数は見た目を描くだけです。所持数、確率、ロール履歴は変更しません。
    // 個数・大きさ・速度を変える場合は、まず tuning/desert.hpp を開いてください。
    const auto& id = f.roller.catalog()[f.presentation.shown(f.progress)].id;
    const float t = f.time;
    const Color sand = color(settings::SandColor), pale = color(settings::PaleSandColor);
    const Color turquoise = color(settings::TurquoiseColor), gold = color(settings::GoldColor);

    // localPoint は「プレイヤーを中心にした位置」をワールドの位置に変換します。
    // x=左右、y=高さ、z=背後。プレイヤーが移動・回転してもオーラが付いてきます。
    const auto avatar = XMMatrixRotationY(f.facing) * XMMatrixTranslation(f.avatarX, 0, f.avatarZ);
    auto localPoint = [&](float x, float y, float z) {
        return XMVector3TransformCoord(XMVectorSet(x, y, z, 1), avatar);
    };
    auto billboard = [&](FXMVECTOR p, float width, float height, Color tint,
                         unsigned style, float opacity, float rotation = 0.f) {
        pixelBillboard(XMVectorGetX(p), XMVectorGetY(p), XMVectorGetZ(p),
                       width, height, tint, style, opacity, f, rotation);
    };
    auto line = [&](FXMVECTOR a, FXMVECTOR b, float width, Color tint, float opacity) {
        pixelLine(a, b, width, tint, opacity, f);
    };
    // 小さな発光板と短い線を、既存の発光バッチでまとめて描きます。
    // 画像や音声の読み込み、新しいシェーダーの作成は必要ありません。
    auto floorRing = [&](float radius, Color tint, float opacity, float speed) {
        for (unsigned i = 0; i < settings::FloorRingSegments; ++i) {
            const float a = i * 2 * Pi / settings::FloorRingSegments + t * speed;
            const float b = (i + 1) * 2 * Pi / settings::FloorRingSegments + t * speed;
            const float wave = 1.f + .035f * std::sin(a * 5 + t);
            const float nextWave = 1.f + .035f * std::sin(b * 5 + t);
            line(localPoint(std::cos(a) * radius * wave, .065f, std::sin(a) * radius * wave),
                 localPoint(std::cos(b) * radius * nextWave, .065f, std::sin(b) * radius * nextWave),
                 .095f, tint, opacity);
        }
    };

    if (id == "dune") {
        // Dune: 足元の輪 → 2 本の砂のらせん → 上に流れる砂粒、の順に追加します。
        floorRing(settings::DuneRadius, sand, .24f, .10f);
        for (unsigned spiral = 0; spiral < 2; ++spiral) {
            auto position = [&](float u) {
                const float angle = u * Pi * 2.2f + t * settings::DuneSpeed + spiral * Pi;
                const float radius = settings::DuneRadius * (1 - u * .30f);
                return localPoint(std::cos(angle) * radius, .12f + u * settings::DuneHeight,
                                  std::sin(angle) * radius);
            };
            for (unsigned j = 0; j < settings::DuneSpiralSegments; ++j) {
                const float u = j / static_cast<float>(settings::DuneSpiralSegments);
                const float v = (j + 1) / static_cast<float>(settings::DuneSpiralSegments);
                // 上に行くほど薄くし、アバターの顔や名前を見やすく残します。
                line(position(u), position(v), settings::DuneLineWidth, sand, .20f * (1 - u * .65f));
            }
        }
        for (unsigned i = 0; i < settings::DuneGrainCount; ++i) {
            const float u = std::fmod(random(i, 2) + t * settings::DuneGrainRiseSpeed, 1.f);
            const float angle = i * 2.399963f + t * settings::DuneSpeed;
            const float radius = settings::DuneRadius * (.62f + random(i, 3) * .50f);
            const float size = settings::DuneGrainSize * (.5f + random(i, 4));
            billboard(localPoint(std::cos(angle) * radius, .10f + u * settings::DuneHeight,
                                 std::sin(angle) * radius), size, size * .72f, i % 3 ? sand : pale,
                      1, .48f * std::sin(u * Pi), angle);
        }
        return;
    }

    if (id == "mirage") {
        // Mirage: 3 本の揺らぐ光。輪を閉じず、陽炎のような途切れた弧にしています。
        floorRing(settings::MirageRadius * .92f, gold, .17f, -.10f);
        for (unsigned arc = 0; arc < 3; ++arc) {
            auto position = [&](float u) {
                const float angle = u * Pi * 1.60f + arc * 2 * Pi / 3 + t * settings::MirageSpeed;
                const float radius = settings::MirageRadius + .14f * std::sin(angle * 3 + t);
                const float y = .50f + arc * .72f +
                    settings::MirageWaveHeight * std::sin(u * Pi * 4 + t * 1.8f + arc);
                return localPoint(std::cos(angle) * radius, y, std::sin(angle) * radius);
            };
            for (unsigned j = 0; j < settings::MirageArcSegments; ++j) {
                const float u = j / static_cast<float>(settings::MirageArcSegments);
                const float v = (j + 1) / static_cast<float>(settings::MirageArcSegments);
                line(position(u), position(v), .12f, arc == 1 ? gold : turquoise,
                     .21f * std::sin((u + .025f) * Pi));
            }
        }
        for (unsigned i = 0; i < settings::MirageCrystalCount; ++i) {
            const float angle = i * 2 * Pi / settings::MirageCrystalCount - t * .22f;
            const float y = .35f + (i % 3) * .49f +
                std::sin(t * 1.5f + i) * settings::MirageCrystalBobHeight;
            const auto p = localPoint(std::cos(angle) * settings::MirageRadius, y,
                                      std::sin(angle) * settings::MirageRadius);
            // 正方形を 45 度回して結晶にし、薄い光を重ねます。
            billboard(p, settings::MirageCrystalSize, settings::MirageCrystalSize * 1.65f,
                      turquoise, 1, .68f, Pi * .25f);
            billboard(p, settings::MirageCrystalSize * 3, settings::MirageCrystalSize * 4,
                      turquoise, 0, .18f);
        }
        for (unsigned i = 0; i < settings::MirageMoteCount; ++i) {
            const float angle = i * 2.399963f + t * .30f;
            const float u = std::fmod(random(i, 7) + t * .13f, 1.f);
            const float radius = settings::MirageRadius * (.65f + random(i, 8) * .45f);
            billboard(localPoint(std::cos(angle) * radius, .20f + u * 2.9f,
                                 std::sin(angle) * radius), .08f, .08f,
                      i % 3 ? turquoise : gold, 4, .32f * std::sin(u * Pi));
        }
        return;
    }

    if (id != "pharaoh") return;
    // Pharaoh: ピラミッドは頂点と底の四隅を細い光で結んで作っています。
    floorRing(settings::PharaohPyramidRadius, gold, .23f, .06f);
    const auto apex = localPoint(0, settings::PharaohPyramidHeight, 0);
    for (unsigned i = 0; i < 4; ++i) {
        const float a = Pi * .25f + i * Pi * .5f + t * settings::PharaohPyramidSpeed;
        const float b = a + Pi * .5f;
        const auto corner = localPoint(std::cos(a) * settings::PharaohPyramidRadius, .10f,
                                       std::sin(a) * settings::PharaohPyramidRadius);
        const auto next = localPoint(std::cos(b) * settings::PharaohPyramidRadius, .10f,
                                     std::sin(b) * settings::PharaohPyramidRadius);
        line(corner, next, .10f, gold, .28f);
        line(corner, apex, .10f, gold, .15f);
        billboard(corner, .22f, .30f, turquoise, 6, .62f, -t * .2f);
    }
    const float hover = std::sin(t * settings::PharaohHoverSpeed) * settings::PharaohHoverHeight;
    // 翼は背後に配置。外側ほど長い羽にし、アバターの正面を隠しません。
    for (int side : {-1, 1}) for (unsigned i = 0; i < settings::PharaohFeatherCount; ++i) {
        const float u = (i + 1) / static_cast<float>(settings::PharaohFeatherCount);
        const float x = side * (.48f + u * settings::PharaohWingWidth);
        const float y = 1.75f + .52f * std::sin(u * Pi * .65f) + hover;
        const float drop = .18f + u * .63f;
        line(localPoint(x, y, .80f), localPoint(x + side * .18f, y - drop, .85f),
             .12f, i % 3 ? gold : turquoise, .38f);
        line(localPoint(side * .48f, 1.75f + hover, .80f), localPoint(x, y, .80f),
             .07f, gold, .16f);
    }
    // 太陽はプレイヤー背後の縦の輪。弱い光と短い光条で金色の紋章にします。
    const float sunY = settings::PharaohSunHeight + hover;
    for (unsigned i = 0; i < settings::PharaohSunSegments; ++i) {
        const float a = i * 2 * Pi / settings::PharaohSunSegments;
        const float b = (i + 1) * 2 * Pi / settings::PharaohSunSegments;
        line(localPoint(std::cos(a) * settings::PharaohSunRadius,
                        sunY + std::sin(a) * settings::PharaohSunRadius, .90f),
             localPoint(std::cos(b) * settings::PharaohSunRadius,
                        sunY + std::sin(b) * settings::PharaohSunRadius, .90f), .10f, gold, .42f);
    }
    for (unsigned i = 0; i < 12; ++i) {
        const float a = i * 2 * Pi / 12 + t * .08f;
        line(localPoint(std::cos(a) * settings::PharaohSunRadius * 1.23f,
                        sunY + std::sin(a) * settings::PharaohSunRadius * 1.23f, .90f),
             localPoint(std::cos(a) * settings::PharaohSunRadius * 1.56f,
                        sunY + std::sin(a) * settings::PharaohSunRadius * 1.56f, .90f), .09f, gold, .34f);
    }
    billboard(localPoint(0, sunY, .90f), 1.5f, 1.5f, gold, 0, .18f);
    for (unsigned i = 0; i < settings::PharaohGlintCount; ++i) {
        const float angle = i * 2.399963f + t * .20f;
        const float u = std::fmod(random(i, 9) + t * .14f, 1.f);
        const float radius = 1.15f + random(i, 10) * 1.2f;
        billboard(localPoint(std::cos(angle) * radius, .20f + u * 3.4f,
                             std::sin(angle) * radius), .10f, .10f,
                  i % 3 ? gold : turquoise, 4, .45f * std::sin(u * Pi));
    }
}
} // namespace aura
