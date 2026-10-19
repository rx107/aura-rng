# 数字を変えてゲームを改造する入口

このフォルダーは、Star's RNG の「よく触る数字」を集めた場所です。各 `.hpp` には、単位、値を増減したときの見た目、小さな変更例を書いています。

初めてビルドする場合は、先に [はじめての改造ガイド](../../docs/development-guide.md) を開いてください。**ソース ZIP の展開 → Visual Studio の「フォルダーを開く」→ 1 行だけ変更 → Ctrl+S → ゲームを閉じる → Develop.cmd → F1 で表示を選ぶ**、の順に案内しています。

## どのファイルを開く？

| 変えたいもの | ファイル | 最初の例 |
| --- | --- | --- |
| ロール間隔・移動・カメラ | [game.hpp](game.hpp) | `RollCooldownSeconds` を `1.4f → .7f`。待ち時間が半分。 |
| ステージ・標準オーラ・Singularity | [world.hpp](world.hpp) | `HalfExtent` を `32.f → 40.f`。床・木・歩行範囲が一緒に広がる。 |
| 惑星の景観・ロケット | [stages.hpp](stages.hpp) | `TravelDurationSeconds` を `3.6f → 5.f`。移動演出が長くなる。木・波・火の粉もここ。 |
| BreakThroughの字幕 | [cutscene.hpp](cutscene.hpp) | `BreakthroughWordTimes` の各秒数。F1からも再ビルドなしで変更可能。 |
| UI の色 | [ui.hpp](ui.hpp) | `Accent` を `{1.f, .55f, .20f}`。ミント色がオレンジになる。 |
| ブロック / VRM 共通のポーズ | [avatar.hpp](avatar.hpp) | `singularity` 内の `Lift` を `.55f → .85f`。少し高く浮く。 |
| Pixelation | [pixelation.hpp](pixelation.hpp) | `FinalMotionSpeed` を `3.f → 4.f`。Lv3 の周囲だけ高速化。 |
| Equinox | [equinox.hpp](equinox.hpp) | `floorGlyphSize` を `3.2f → 2.8f`。床の「衡」が小さくなる。 |
| BreakThrough の通常表示 | [breakthrough.hpp](breakthrough.hpp) | `titleHeight` を `3.55f → 3.9f`。名前を上に移動。 |
| Nanally | [nanally.hpp](nanally.hpp) | `HeadWidth` を `6.35f → 7.0f`。頭が横に広がる。 |

オーラ名、確率、標準の輪・粒子の色や数は [data/auras.csv](../../data/auras.csv) を使います。特殊なオーラの形は同名の専用 `.hpp` で調整します。安全な試作用オーラを CSV に追加する例も、[改造ガイド](../../docs/development-guide.md#6-csv-で自作オーラを-1-個追加する) にあります。

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

- **Pixelation は時計が 2 本**あります。`FinalMotionSpeed` は Lv3 の周囲の回転用、`BoostedSpeed` は Lv2 / Lv3 で共通の弾・爆発・浮遊用でもあります。詳細は `pixelation.hpp` の冒頭へ。
- **BreakThrough の通常表示と CutScene は独立**しています。このフォルダーの柱設定を変えても、CutScene の柱は変わらず静止します。
- **数は少しずつ増やす**と試しやすくなります。粒子や線の数を増やすと描画量も増えます。各ファイルの範囲チェックを残したまま試してください。
- **RGB は赤・緑・青**の順番です。UI の色は各 0〜1、専用オーラの発光は 1 を超える成分も使います。

コード側では `aura::tuning::nanally::HeadWidth` のように参照しています。数字を変更して保存すると、次のビルドからその値が使われます。処理そのものを追いたくなったら、[改造ガイドのコード地図](../../docs/development-guide.md#8-処理を追いたくなったときの地図) を使ってください。
