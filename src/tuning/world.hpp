#pragma once

namespace aura::tuning::world {
// ステージの「半分」の幅 [ワールド単位]。32.fなら横64×縦64。
// 例: 40.fなら80×80に広がり、床タイル・外周の木の位置・歩ける範囲も追従します。
inline constexpr float HalfExtent = 32.f;
inline constexpr float EdgeWalkPadding = 1.2f; // 端からこの距離だけ手前で移動を止める。
inline constexpr float TileSize = 2.f; // 床1マスの幅。大きいほどタイルが大きく、枚数は減る。
inline constexpr int PathHalfTiles = 1; // 中央通路の左右それぞれのマス数。1なら通路は3マス幅。

// ここからは標準オーラ/Singularityの見た目。特殊オーラには各専用.hppがあります。
inline constexpr unsigned StandardRingSegments = 48; // 足元の輪の点の数。多いほど滑らか。
inline constexpr float StandardRingHeight = .25f; // 足元の輪の高さ。
inline constexpr float StandardRingAngularSpeed = .5f; // 足元の輪の回転速度。0なら回転停止。
inline constexpr float StandardParticleRiseSpeed = .38f; // 標準の粒子が上に進む速さ。
inline constexpr float SmallBlackHoleRadius = .72f; // Singularity Lv2のブラックホール半径。
inline constexpr float LargeBlackHoleRadius = 1.65f; // Lv3の半径。2.f等にすると大きくなる。
inline constexpr float BlackHoleHeight = 5.7f; // ブラックホールの中心の高さ。

// 以下は上の値から自動計算する値です。通常は変更しません。
inline constexpr int TileHalfCount = static_cast<int>(HalfExtent / TileSize) - 1;
inline constexpr float PropLayoutScale = HalfExtent / 32.f;
static_assert(HalfExtent >= 12.f && HalfExtent <= 48.f, "HalfExtent: use 12..48 to stay inside the lightweight scene budget");
static_assert(TileSize >= 1.f && TileSize <= 4.f && TileHalfCount >= 1 && TileHalfCount <= 28,
    "Too many floor tiles: increase TileSize or reduce HalfExtent");
static_assert(PathHalfTiles >= 0 && PathHalfTiles <= TileHalfCount);
static_assert(EdgeWalkPadding > 0.f && EdgeWalkPadding < HalfExtent);
static_assert(StandardRingSegments >= 8 && StandardRingSegments <= 128);
static_assert(StandardRingAngularSpeed >= 0.f && StandardParticleRiseSpeed >= 0.f);
static_assert(SmallBlackHoleRadius > 0.f && LargeBlackHoleRadius > 0.f);
}
