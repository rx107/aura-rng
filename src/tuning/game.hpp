#pragma once

// ゲーム全体の「触りやすい設定」です。値を保存 → Develop.cmd で作り直すと反映されます。
// まず1つだけ変更して見比べてみましょう。詳しくは docs/development-guide.md。
namespace aura::tuning::game {

// ロール間隔 [秒]。1.4f → 0.7f で待ち時間が半分。手動とAutoRollの両方に反映。
inline constexpr float RollCooldownSeconds = 1.4f;

// 移動速度 [ワールド単位/秒]。3.f → 4.5f で1.5倍速く歩きます。
inline constexpr float WalkSpeed = 3.f;
// 歩くときのアニメーション時計の進み方。大きくすると足の動きが速くなります。
inline constexpr float WalkPhaseSpeed = 3.f;

// カメラの初期位置。距離を11.f → 14.fにすると遠くから全体を見渡せます。
inline constexpr float InitialCameraDistance = 11.f;
// 角度はラジアン。0.fが正面、約1.57fが横、約3.14fが背面。
inline constexpr float InitialCameraYaw = .45f;
// 上から見下ろす角度。大きいほど俯瞰。0.52fは約30度。
inline constexpr float InitialCameraPitch = .52f;

// 右ドラッグの感度 [ラジアン/ピクセル]。小さくするとゆっくり回せます。
inline constexpr float OrbitHorizontalSensitivity = .008f;
inline constexpr float OrbitVerticalSensitivity = .006f;
inline constexpr float MinimumCameraPitch = .12f;
inline constexpr float MaximumCameraPitch = 1.28f;
// ホイール1目盛で変わる距離。大きくするとズームが速くなります。
inline constexpr float ZoomStep = .6f;
inline constexpr float MinimumCameraDistance = 6.f;
inline constexpr float MaximumCameraDistance = 34.f;
// 視野角 [ラジアン]。大きくすると広く映ります（画面端の歪みも増えます）。
inline constexpr float FieldOfView = .80f;

// カメラが注目する高さ。大きなオーラの上側を見切れにくくするために切り替えます。
inline constexpr float CameraFocusNormal = 1.4f;
inline constexpr float CameraFocusFloating = 2.f;
inline constexpr float CameraFocusEquinox = 2.3f;
inline constexpr float CameraFocusBlackHole = 2.5f;
inline constexpr float CameraFocusNanally = 2.8f;
inline constexpr float CameraFocusBreakthrough = 3.3f;

// static_assert は「実行前の設定チェック」。無効な値なら説明付きでビルドを止めます。
static_assert(RollCooldownSeconds > 0.f, "RollCooldownSeconds must be positive");
static_assert(WalkSpeed > 0.f && WalkPhaseSpeed >= 0.f, "Walk speed must be positive");
static_assert(MinimumCameraDistance > 0.f && MinimumCameraDistance < MaximumCameraDistance);
static_assert(InitialCameraDistance >= MinimumCameraDistance && InitialCameraDistance <= MaximumCameraDistance);
static_assert(MinimumCameraPitch >= 0.f && MinimumCameraPitch < MaximumCameraPitch && MaximumCameraPitch < 1.57f);
static_assert(InitialCameraPitch >= MinimumCameraPitch && InitialCameraPitch <= MaximumCameraPitch);
static_assert(OrbitHorizontalSensitivity > 0.f && OrbitVerticalSensitivity > 0.f && ZoomStep > 0.f);
static_assert(FieldOfView > .1f && FieldOfView < 2.5f, "FieldOfView must be between 0.1 and 2.5 radians");
}
