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
    auto floorRing = [&](float radius, Color tint, float opacity, float speed, float width = .095f) {
        for (unsigned i = 0; i < settings::FloorRingSegments; ++i) {
            const float a = i * 2 * Pi / settings::FloorRingSegments + t * speed;
            const float b = (i + 1) * 2 * Pi / settings::FloorRingSegments + t * speed;
            const float wave = 1.f + .035f * std::sin(a * 5 + t);
            const float nextWave = 1.f + .035f * std::sin(b * 5 + t);
            line(localPoint(std::cos(a) * radius * wave, .065f, std::sin(a) * radius * wave),
                 localPoint(std::cos(b) * radius * nextWave, .065f, std::sin(b) * radius * nextWave),
                 width, tint, opacity);
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
    // Pharaoh: 輪郭を太くし、足元は金色の二重輪と放射状の細工にします。
    // 不透明度は控えめにしておき、明るさを足す代わりに「線の幅」で存在感を出します。
    floorRing(settings::PharaohPyramidRadius, gold, .23f, .06f, settings::PharaohFloorLineWidth);
    floorRing(settings::PharaohPyramidRadius * settings::PharaohFloorInnerRadius,
              gold, .18f, .06f, settings::PharaohFloorLineWidth * .70f);
    for (unsigned i = 0; i < 8; ++i) {
        const float a = i * Pi * .25f + t * .06f;
        const float inner = settings::PharaohPyramidRadius * (settings::PharaohFloorInnerRadius + .04f);
        const float outer = settings::PharaohPyramidRadius * .96f;
        line(localPoint(std::cos(a) * inner, .07f, std::sin(a) * inner),
             localPoint(std::cos(a) * outer, .07f, std::sin(a) * outer),
             settings::PharaohFloorLineWidth * .60f, gold, .20f);
        billboard(localPoint(std::cos(a) * inner, .08f, std::sin(a) * inner),
                  .13f, .13f, turquoise, 1, .30f, Pi * .25f);
    }
    const auto apex = localPoint(0, settings::PharaohPyramidHeight, 0);
    for (unsigned i = 0; i < 4; ++i) {
        const float a = Pi * .25f + i * Pi * .5f + t * settings::PharaohPyramidSpeed;
        const float b = a + Pi * .5f;
        const auto corner = localPoint(std::cos(a) * settings::PharaohPyramidRadius, .10f,
                                       std::sin(a) * settings::PharaohPyramidRadius);
        const auto next = localPoint(std::cos(b) * settings::PharaohPyramidRadius, .10f,
                                     std::sin(b) * settings::PharaohPyramidRadius);
        line(corner, next, settings::PharaohPyramidLineWidth, gold, .25f);
        line(corner, apex, settings::PharaohPyramidLineWidth, gold, .19f);
        billboard(corner, .22f, .30f, turquoise, 6, .62f, -t * .2f);
    }
    const float hover = std::sin(t * settings::PharaohHoverSpeed) * settings::PharaohHoverHeight;
    // 翼は背後に配置。長い風切羽、その上に重なる短い羽、青緑の宝石の三層です。
    // 羽の外周を二本で囲み、中に空きを残すので、光が重なっても一枚ずつ読めます。
    for (int side : {-1, 1}) {
        auto wingPoint = [&](float u, float lower = 0.f, float depth = 1.05f) {
            return localPoint(side * (.48f + u * settings::PharaohWingWidth),
                              1.83f + settings::PharaohWingHeight * std::sin(u * Pi * .63f) + hover - lower,
                              depth);
        };
        for (unsigned i = 0; i < settings::PharaohFeatherCount; ++i) {
            const float u = (i + 1) / static_cast<float>(settings::PharaohFeatherCount);
            const float previous = i / static_cast<float>(settings::PharaohFeatherCount);
            const float x = side * (.48f + u * settings::PharaohWingWidth);
            const float y = 1.83f + settings::PharaohWingHeight * std::sin(u * Pi * .63f) + hover;
            const float drop = .32f + u * settings::PharaohFeatherLength;
            const auto tip = localPoint(x + side * (.20f + .18f * u), y - drop, 1.10f);
            line(wingPoint(previous), wingPoint(u), settings::PharaohWingLineWidth * 1.12f, gold, .31f);
            line(localPoint(x - side * .075f, y, 1.06f), tip,
                 settings::PharaohWingLineWidth, gold, .27f);
            line(localPoint(x + side * .13f, y - .035f, 1.06f), tip,
                 settings::PharaohWingDetailWidth * 1.25f, pale, .21f);
            // 青緑の芯は四枚おき。全部を同じ色で埋めず、金の輪郭と対比させます。
            line(localPoint(x + side * .035f, y - .12f, 1.035f),
                 localPoint(x + side * (.18f + .18f * u), y - drop * .88f, 1.085f),
                 settings::PharaohWingDetailWidth * .70f, i % 4 ? gold : turquoise, .20f);
            // 短い羽を手前に重ねると、付け根に宝飾の鱗が並ぶように見えます。
            const auto coverTip = localPoint(x + side * .09f, y - drop * .43f, .99f);
            line(localPoint(x - side * .11f, y - .025f, .99f), coverTip,
                 settings::PharaohWingDetailWidth * 1.50f, gold, .26f);
            line(localPoint(x + side * .12f, y - .055f, .99f), coverTip,
                 settings::PharaohWingDetailWidth, pale, .18f);
            if (i % 4 == 1) {
                const auto gem = localPoint(x, y - .10f, .96f);
                billboard(gem, .20f, .26f, gold, 1, .27f, Pi * .25f);
                billboard(gem, .105f, .16f, turquoise, 1, .48f, Pi * .25f);
                billboard(gem, .34f, .40f, turquoise, 0, .08f);
            }
        }
        // 肩の飾りから翼の扇へつながる二重の金色の帯です。
        line(localPoint(side * .30f, 1.67f + hover, .99f), wingPoint(.19f),
             settings::PharaohWingLineWidth, gold, .29f);
        line(localPoint(side * .30f, 1.52f + hover, .99f), wingPoint(.19f, .18f),
             settings::PharaohWingDetailWidth, turquoise, .20f);
        billboard(localPoint(side * .53f, 1.82f + hover, .98f), .32f, .46f,
                  gold, 1, .28f, Pi * .25f);
        billboard(localPoint(side * .53f, 1.82f + hover, .95f), .17f, .26f,
                  turquoise, 1, .48f, Pi * .25f);
    }
    // 太陽も翼に合わせて少し拡大。二重の円、長短の光条、控えめな青緑の点で統一します。
    const float sunY = settings::PharaohSunHeight + hover;
    for (unsigned i = 0; i < settings::PharaohSunSegments; ++i) {
        const float a = i * 2 * Pi / settings::PharaohSunSegments;
        const float b = (i + 1) * 2 * Pi / settings::PharaohSunSegments;
        line(localPoint(std::cos(a) * settings::PharaohSunRadius,
                        sunY + std::sin(a) * settings::PharaohSunRadius, .90f),
             localPoint(std::cos(b) * settings::PharaohSunRadius,
                        sunY + std::sin(b) * settings::PharaohSunRadius, .90f),
             settings::PharaohSunLineWidth, gold, .29f);
        line(localPoint(std::cos(a) * settings::PharaohSunRadius * .76f,
                        sunY + std::sin(a) * settings::PharaohSunRadius * .76f, .90f),
             localPoint(std::cos(b) * settings::PharaohSunRadius * .76f,
                        sunY + std::sin(b) * settings::PharaohSunRadius * .76f, .90f),
             settings::PharaohSunLineWidth * .57f, pale, .16f);
    }
    for (unsigned i = 0; i < settings::PharaohSunRayCount; ++i) {
        const float a = i * 2 * Pi / settings::PharaohSunRayCount + t * .08f;
        const float rayStart = settings::PharaohSunRadius + .11f;
        const float rayEnd = rayStart + settings::PharaohSunRayLength * (i % 2 ? .62f : 1.f);
        line(localPoint(std::cos(a) * rayStart, sunY + std::sin(a) * rayStart, .90f),
             localPoint(std::cos(a) * rayEnd, sunY + std::sin(a) * rayEnd, .90f),
             settings::PharaohSunLineWidth * .83f, gold, .28f);
        if (i % 4 == 0)
            billboard(localPoint(std::cos(a) * rayEnd, sunY + std::sin(a) * rayEnd, .90f),
                      .13f, .18f, turquoise, 1, .42f, a);
    }
    billboard(localPoint(0, sunY, .90f), settings::PharaohSunRadius * 2.6f,
              settings::PharaohSunRadius * 2.6f, gold, 0, .11f);
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
