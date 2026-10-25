# 数字を変えてゲームを改造する入口

このフォルダーは、Star's RNG の「よく触る数字」を集めた場所です。各 `.hpp` には、単位、値を増減したときの見た目、小さな変更例を書いています。

初めてビルドする場合は、先に [はじめての改造ガイド](../../docs/development-guide.md) を開いてください。**ソース ZIP の展開 → Visual Studio の「フォルダーを開く」→ 1 行だけ変更 → Ctrl+S → ゲームを閉じる → Develop.cmd → F1 で表示を選ぶ**、の順に案内しています。

## どのファイルを開く？

| 変えたいもの | ファイル | 最初の例 |
| --- | --- | --- |
| ロール間隔・移動・カメラ | [game.hpp](game.hpp) | `RollCooldownSeconds` を `1.4f → .7f`。待ち時間が半分。 |
| ステージ・標準オーラ・Singularity | [world.hpp](world.hpp) | `HalfExtent` を `32.f → 40.f`。床・木・歩行範囲が一緒に広がる。 |
| 惑星の景観・ロケット・Extraの星 | [stages.hpp](stages.hpp) | `ExtraInnerRadiusRatio` を `.55f → .65f`。星の谷が浅くなり、ふっくらする。 |
| カットシーンのカメラ・揺れ・Equinox終了フェード | [cutscene.hpp](cutscene.hpp) | `EndingFadeSeconds` を `.8f → 1.2f`。Equinoxの映像と音がゆっくりオーラへ切り替わる。BreakThroughは終了時に即切替。 |
| BreakThroughの字幕 | [cutscene.hpp](cutscene.hpp) | `BreakthroughWordTimes` の各秒数。字幕の時差と単語の時刻はF1からも再ビルドなしで変更可能。 |
| UI の色 | [ui.hpp](ui.hpp) | `Accent` を `{1.f, .55f, .20f}`。ミント色がオレンジになる。 |
| ブロック / VRM 共通のポーズ | [avatar.hpp](avatar.hpp) | `singularity` 内の `Lift` を `.55f → .85f`。少し高く浮く。 |
| Pixelationの機体・尾・キューブ | [pixelation.hpp](pixelation.hpp) | `PlayerBobAmplitude` を `.85 → .4`。PLAYERの上下移動が小さくなる。 |
| Equinoxの玉・速度の強弱・爆発・床回転 | [equinox.hpp](equinox.hpp) | `orbitSpeedVariation` を `.22f → 0.f`。玉の加減速をなくして等速で周回する。 |
| BreakThrough の通常表示 | [breakthrough.hpp](breakthrough.hpp) | `titleHeight` を `3.85f → 4.1f`。名前を上に移動。 |
| Nanally | [nanally.hpp](nanally.hpp) | `HeadWidth` を `6.35f → 7.0f`。頭が横に広がる。 |
| Illusionaryの床・白い光・文字 | [illusionary.hpp](illusionary.hpp) | `OrbitLightRadius` を `1.92f → 2.2f`。白い光が体から離れた場所を回る。 |
| 砂漠テーマ3種類 | [desert.hpp](desert.hpp) | `PharaohWingWidth` を `3.45f → 4.f`。長短二段の翼が横へ広がる。Dune / Mirageにも個別の半径・速度・粒子数。 |
| Frutiger Aeroの草原・空・泡・青い蝶 | [frutiger.hpp](frutiger.hpp) | `MeadowRadius` を `4.7f → 5.5f`。丸い草原と小川が広がる。[調整の手順](../../docs/frutiger-aero.md)。 |
| Red johnの部屋・ベッド・赤い顔・CutScene | [redjohn.hpp](redjohn.hpp) | `FaceRadius`を`1.06f → 1.25f`で壁の赤い顔が大きくなる。座り方は[avatar.hpp](avatar.hpp)で変更。[最初の手順](../../docs/red-john-guide.md)。 |

オーラ名、確率、標準の輪・粒子の色や数は [data/auras.csv](../../data/auras.csv) を使います。特殊なオーラの形は同名の専用 `.hpp` で調整します。安全な試作用オーラを CSV に追加する例も、[改造ガイド](../../docs/development-guide.md#6-csv-で自作オーラを-1-個追加する) にあります。

出現する惑星とCollectionの分類は `src/core/aura.cpp` の `auraCategory()` / `availableInBiome()`が担当します。Normalは全惑星、Desertは砂漠、ExtraはExtraで入手できます。AdministratorのNanallyはプレビュー専用です。分類と入手方法の一覧は[IllusionaryとExtraのガイド](../../docs/illusionary-and-extra-guide.md)へ。

## 1 回の編集の流れ

1. まず 1 個の数字だけ変えます。たとえば `nanally.hpp` の `HeadWidth = 6.35f` を `HeadWidth = 7.0f` にします。
2. **Ctrl+S** で保存します。末尾の `f`、`;`、`{ }` などは残します。
3. ゲームを閉じ、プロジェクト直下の **Develop.cmd** を実行します。
4. 起動したゲームの **F1 → オーラ / Auras** で対象を表示します。Lv 専用の設定は同じレベルで比較します。
5. 元へ戻すときも、元の数字に戻して同じ手順を行います。

**保存だけでは実行中のゲームは変わりません。再ビルドが必要です。** `Develop.cmd` は `build.cmd --build-only` でコンパイルし、デバッグ画面付きでゲームを開きます。セーブは通常起動と分け、プロジェクト直下の `local-save` を使います。

通常の `build.cmd` は標準値を想定した回帰テストも行います。見た目の値を意図して変えた場合でも、標準の姿勢や速度との比較でテストが失敗することがあります。見た目の試行は `Develop.cmd`、標準設定の動作確認は `build.cmd`、と使い分けてください。設定の `static_assert` による範囲チェックやコンパイルエラーは、Develop.cmd でも有効です。

CSV を変えた場合も元の `data/auras.csv` を保存し、Develop.cmd を再実行します。開発起動時に `build-release/data` へコピーされ、起動時に読み込まれます。`build-release` 内のコピーを直接編集すると、次回のコピーで上書きされます。

## 速度と範囲について

- **Pixelation は時計が 2 本**あります。`FinalMotionSpeed` は Lv3 の周囲の回転用、`BoostedSpeed` は Lv2 / Lv3 で共通の弾・爆発・浮遊用でもあります。機体だけの速さは`PlayerOrbitSpeed = 1.55`、上下幅は`PlayerBobAmplitude = .85`です。機体の速度を変えても弾が衝突する間隔は変わりません。
- **Pixelationの光の尾**は`PlayerTrailDuration = .72f`で長さ、`PlayerTrailSegments = 28`で滑らかさを調整します。時間を長くすると飛んできた軌道を長く残し、分割数を増やすとカーブが滑らかになります。尾は機体と同じ3D軌道をたどります。
- **キューブの鼓動はBGMの再生時刻**を使います。`CageBeatScale = .15f`は1拍で最大15%拡大、`CageBeatBrightness = .20f`は最大20%増光。0でそれぞれの変化を止めます。拍の速さと時差はF1 → 音楽 → キューブの鼓動から変更でき、再ビルド不要です。自動推定が半分・倍に感じる場合はBPMを30〜300で手動指定します。音楽を再生していない間とBGMのないLv0は鼓動しません。
- **Equinoxは一つの拍の時計を共有**します。通常BGMとCutSceneはF1 → 音楽でBPM・時差を別々に保存します。手動BPMは30〜300、時差は-2〜+2秒、正の値で演出の拍を遅らせます。通常表示では`orbitBeats = 4.f`が4拍で1周、`polarityBeats = 1.f`が1拍で白黒交換、`floorGlyphBeats = 4.f`が4拍で次の床文字です。「陰→陽→衡→無」の一巡は16拍になります。
- **Equinoxの玉と尾は別の大きさ**です。`orbRadius = 1.80f`を下げると玉だけが小さくなり、`orbOrbitRadius = 3.60f`を上げると周回範囲が広がります。`orbHeight = 2.70f`で玉の高さを変更します。`trailBeats = .80f`なら過去0.8拍分の位置を先細りの尾でつなぎ、`trailWidth = .20f`で太さを変えます。以前の波形軌跡と点列の星は描きません。
- **Equinoxの速度に強弱を付ける値**は`orbitSpeedVariation = .22f`です。平均の78〜122%で加減速し、`0.f`で等速になります。`orbitSpeedCycleOrbits = 1.f`は1周に1回の強弱、`2.f`なら2周かけて1回変わります。白黒交換・床文字の拍は変えず、玉の位置と尾と爆発の発生位置が同じ計算を使います。強弱は`0`以上`1`未満に保ちます。
- **Equinoxの床と粒子は一緒に回ります。**`floorRotation = 1.10f`が共通の回転速度です。`.55f`で半分、`0.f`で回転を止め、負にすると逆方向になります。交換時の爆発は`burstRadius = 1.65f`で広がる距離、`burstBrightness = .82f`で明るさ、`burstFragments = 18`で破片の数を変えます。`burstRingWidth = .15f`と`burstFragmentWidth = .070f`は輪と破片の太さです。
- **BreakThrough の通常表示と CutScene は独立**しています。`breakthrough.hpp`の柱設定を変えても、CutScene の柱は変わらず静止します。`cutscene.hpp`の`BreakthroughOriginalI / BreakthroughOriginalII`は初実装のi・ii、`BreakthroughCoreRush`はiii、`BreakthroughMonolith`で始まる値はivを調整します。`BreakthroughBlackoutEnd`は黒画面が終わる時刻、`BreakthroughFinalDistance`・`BreakthroughFinalPullbackSpeed`・`BreakthroughFinalYawSpeed`は15.05秒以降の7本柱を映すカメラです。[場面と調整値の説明](../../docs/stage-and-cutscene-guide.md#breakthroughの場面とカメラ)に値の意味をまとめています。
- **後半の2つのテロップは同じ動き**です。`BreakthroughFloatingCaptionAngle = -2.f`は傾きの度数、`BreakthroughFloatingCaptionSpeed = 6.f`は1秒ごとの横移動、`BreakthroughFloatingCaptionMoveSeconds = 4.f`は移動を続ける秒数です。`it wasn't gone`と`with only 5 fragments left`の両方に反映されます。
- **BreakThroughのiv字幕中だけ白黒二値**になります。`BreakthroughIvCaptionStart = 13.f`以上、`BreakthroughIvCaptionEnd = 14.12f`未満を`breakthroughIvCaptionVisible()`で共有します。`BreakthroughIvInkThreshold = .12f`は白と黒を分ける明るさの境界です。直前の閃光と`get out of my head`以降にはかかりません。
- **BreakThroughは終端で即切り替え**ます。`endingOpacity()`に`instantSwitch=true`を渡すため、`EndingFadeSeconds`の変更はEquinoxだけに影響します。BreakThroughの映像内にある冒頭0.25秒の導入フェード、i・iiの絞り閉鎖・開口、iii冒頭の開口、最後の絞り閉鎖・短い閃光は保ちます。これらは通常画面へ重ねる終了フェードとは別の演出です。
- **Illusionaryの天を仰ぐポーズ**は `avatar.hpp` の `illusionary`にあります。`BodyPitch`が全身の傾き、`TorsoPitch`が胸の反り、`HeadPitch`が首の反りです。`ElbowPitch`の片方だけを約1.5708にすると直角の肘になります。`illusionary.hpp`は光・帯・床・文字を担当し、姿勢とは分けています。
- **文字と光の明るさは別々**に調整します。Illusionaryの `TextSelfLight`はにじまない文字用です。`White`を変えると白い光全体にも影響します。
- **Equinoxの揺れは保存したBPMと時差**で拍を合わせます。`EquinoxBassShakeHeight`は画面高に対する割合で、`.008f`なら0.8%、`0.f`なら無効です。名前にBassが残っていますが、生の低音の強さをそのまま揺れ幅へ使う方式ではありません。拍はF1で合わせ、揺れの最大幅はこのC++の値で変えます。
- **Pharaohは明るさより線幅で形を出します。**`PharaohPyramidLineWidth = .30f`がピラミッド、`PharaohFloorLineWidth = .28f`が床の外輪です。`PharaohWingHeight = 1.20f`を上げると翼の外側が高くなり、`PharaohFeatherLength = 1.28f`を上げると外側の羽が下へ伸びます。`PharaohSunRadius = .78f`と`PharaohSunLineWidth = .28f`で太陽の大きさと輪郭を独立に調整できます。標準値は最大437枚の板なので、羽や円の分割数は少しずつ増やしてください。
- **数は少しずつ増やす**と試しやすくなります。粒子や線の数を増やすと描画量も増えます。各ファイルの範囲チェックを残したまま試してください。
- **RGB は赤・緑・青**の順番です。UI の色は各 0〜1、専用オーラの発光は 1 を超える成分も使います。

コード側では `aura::tuning::nanally::HeadWidth` のように参照しています。数字を変更して保存すると、次のビルドからその値が使われます。処理そのものを追いたくなったら、[改造ガイドのコード地図](../../docs/development-guide.md#8-処理を追いたくなったときの地図) を使ってください。
