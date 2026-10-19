# はじめての改造ガイド

まずは「数字を1つ変えて、画面が変わる」を体験しましょう。C++を全部理解してから始める必要はありません。

## 1. 改造用のソースを用意する

[改造用ソースZIP](https://github.com/rx107/aura-rng/archive/refs/tags/v0.1.11-alpha.1.zip)をダウンロードし、右クリック → **すべて展開**します。ZIPの中から直接実行せず、展開先を開いてください。

`CMakeLists.txt`、`Develop.cmd`、`src`、`data` が同じ階層に見える場所がプロジェクトの入口です。遊ぶためのWindows ZIPは完成したEXEだけなので、改造にはこのソースZIPを使います。

コードをEXEに作り直すことを「ビルド」と呼びます。そのためのMSVC、Windows SDK、CMakeが必要です。Visual Studio Installerで **C++によるデスクトップ開発** を選び、MSVC x64/x86、Windows SDK、C++ CMakeツールを用意してください。[Microsoft公式の日本語手順](https://learn.microsoft.com/ja-jp/cpp/build/vscpp-step-0-installation?view=msvc-170)でワークロードの選び方を確認できます。

本作の手元での検証環境はVisual Studio 2022 / MSVC 19.39です。`build.cmd`はインストール済みのMSVC x64環境を自動検出します。**Visual Studio Codeというエディターだけを入れても、C++コンパイラーは別途必要**です。

Visual Studioの **ファイル → 開く → フォルダー** から、展開したプロジェクトを開きます。新しい空のプロジェクトを作る必要はありません。

## 2. 最初にNanallyの頭を少し大きくする

1. フォルダー一覧で `src` → `tuning` → `nanally.hpp` を開きます。
2. 検索（Ctrl+F）で `HeadWidth` を探します。
3. 次の1行の **6.35だけを7.0へ変更**します。

```cpp
// 変更前：頭の横幅
inline constexpr float HeadWidth = 6.35f;

// 変更後：少し横に大きな頭にする
inline constexpr float HeadWidth = 7.0f;
```

4. **Ctrl+Sで保存**します。
5. 起動中のStar's RNGを閉じます。実行中のEXEは上書きできません。
6. プロジェクト直下の **Develop.cmdをダブルクリック**します。
7. 黒い画面でビルドが進み、成功するとゲームとF1パネルが開きます。初回は時間がかかります。
8. 必要ならLanguageを日本語へ変更し、**Auras / オーラ → Nanallyを表示 / 試作** を選びます。

頭が横に広がっていれば成功です。元に戻すときは `6.35f` に戻して保存し、同じ手順を行います。

**保存しただけでは、起動中のゲームは変わりません。C++の変更には再ビルドと再起動が必要です。** F1に元からある服の色やLuckは、その場で変えられます。

## 3. どこを変えると何が変わる？

まずは表の中から1つだけ試してください。各ファイルのコメントに詳しい意味と範囲があります。

| やってみたいこと | ファイルと設定 | 変更例と結果 |
| --- | --- | --- |
| ロールを速くする | `src/tuning/game.hpp` → `RollCooldownSeconds` | `1.4f` → `.7f` で待ち時間が半分。AutoRollにも反映 |
| 移動を速くする | 同上 → `WalkSpeed` | `3.f` → `4.5f` で1.5倍の移動速度 |
| 遠くから見る | 同上 → `InitialCameraDistance` | `11.f` → `14.f`。起動時のカメラが遠くなる |
| ステージを広げる | `src/tuning/world.hpp` → `HalfExtent` | `32.f` → `40.f`。床が64×64から80×80へ。木の配置と歩ける範囲も広がる |
| 惑星移動をゆっくりにする | `src/tuning/stages.hpp` → `TravelDurationSeconds` | `3.6f` → `5.f`。ロケット演出が長くなる |
| 森の木を高くする | 同上 → `ForestTreeHeight` | `4.6f` → `6.f`。森を大きな木々が囲む |
| サックスと字幕を合わせる | F1 → オーラ → BreakThrough → 字幕タイミング / 秒 | 全体の時差 `-.10f` で0.1秒早く。再ビルド不要。[操作手順](stage-and-cutscene-guide.md) |
| UIの色を変える | `src/tuning/ui.hpp` → `Accent` | `{1.f,.55f,.20f}` で共通のミント色をオレンジへ |
| 無重力ポーズを高くする | `src/tuning/avatar.hpp` → `singularity` の `Lift` | `.55f` → `.85f`。ブロックとVRMが少し高く浮く |
| Pixelation Lv3の周囲だけ速くする | `src/tuning/pixelation.hpp` → `FinalMotionSpeed` | `3.f` → `4.f`。弾と浮遊の速さは変えない |
| Equinoxの床文字を小さくする | `src/tuning/equinox.hpp` → `floorGlyphSize` | `3.2f` → `2.8f` |
| BreakThroughの名前を上げる | `src/tuning/breakthrough.hpp` → `titleHeight` | `3.55f` → `3.9f` |
| Nanallyの電撃を太くする | `src/tuning/nanally.hpp` → `ElectricWidth` | `.12f` → `.18f`。小さな稲妻の線が太くなる |
| Illusionaryの回転をゆっくりにする | `src/tuning/illusionary.hpp` → `MotionSpeed` | `1.f` → `.7f`。リボンと放射光の動きがゆっくりになる |
| 砂漠オーラの砂を広げる | `src/tuning/desert.hpp` → `DuneRadius` | `1.75f` → `2.2f`。砂のらせんが外側へ広がる |
| ロケットの乗降空間を高くする | `src/tuning/stages.hpp` → `RocketBoardingClearance` | `3.f` → `4.f`。機体だけ1単位上へ移動。発着地点は中央のまま |

`HalfExtent`は全幅の**半分**です。32なら全幅64。処理を軽く保つため、範囲チェックでは12〜48を指定できるようにしています。

Pixelationには時計が2本あります。**Motion**は周囲の粒子・立方体・PLAYERの回転、**Action**は弾・衝突・爆発・アバター浮遊です。Lv2/3のActionは同じ設定を使うので、同じ速さを保ちます。

BreakThroughの調整ファイルは**通常表示用**です。柱の浮遊幅や位置を変えても、別の処理で描くCutSceneの柱は変更されません。CutSceneでは柱を静止させています。

## 4. コードの記号を少しだけ読む

```cpp
// この行は人が読む説明。ゲームの処理にはなりません。
inline constexpr float HeadWidth = 7.0f;
```

- `HeadWidth`：設定の名前です。大文字・小文字も区別します。
- `7.0f`：数値です。末尾の`f`はfloat型の数値という印。最初は数字だけを変え、`f`は残します。
- `;`：文の終わり。消すとコンパイルエラーになります。
- `inline constexpr`：ビルド時に決まる共有の定数。保存後に作り直すと新しい値を使います。
- `.hpp`：設定やデータの形を共有するファイル。`.cpp`：実際の処理を書くファイル。
- `{赤, 緑, 青}`：RGBの順。通常の色は0〜1。発光用の色には1を超える値も使います。
- `x/y/z`：左右・高さ・前後の位置。オーラの設定は多くがプレイヤー基準です。見える左右はカメラの向きでも変わります。
- `sin(時間×速度)×幅`：上下や左右を往復する動き。速度を大きくすると速く、幅を大きくすると大きく揺れます。

たとえば `HoverAmplitude` は揺れる幅、`HoverSpeed` はその速度です。正確な設定名は各ファイルを検索してください。似た名前を自分で書き足すだけでは反映されません。

## 5. 開発中のセーブとテスト

`Develop.cmd`からの起動は、プロジェクト内の **local-save** を使います。通常の `StarRng.exe` は `%LOCALAPPDATA%\AuraRng` を使います。開発側の所持数や設定が最初は異なるのは、保存先を分けているためです。

| 起動方法 | 何をする？ |
| --- | --- |
| `Develop.cmd` | コンパイルとデータコピー → F1付きで起動。試作用セーブを使う |
| `build.cmd` | コンパイルとデータコピー → 標準値向けの回帰テスト。ゲームは自動起動しない |
| `build.cmd --build-only` | コンパイルとデータコピーだけ。Develop.cmdから内部で使う |

回帰テストは「元の仕様が壊れていないか」を確かめるものです。標準の確率、ステージ幅、ポーズ、速度を確認する項目があるため、意図して値を変えた場合にも失敗することがあります。**見た目や速度を試すときはDevelop.cmd**、標準設定で動作確認するときはbuild.cmdを使います。設定の範囲チェックやC++の構文エラーは、Develop.cmdでも検出します。

## 6. CSV で自作オーラを 1 個追加する

`data/auras.csv`を開き、末尾に次の1行を追加します。既存の行や先頭のヘッダーはそのまま残します。

```csv
auratest,My Test Aura,0,1.0,0.3,0.7,32,1.5,0.7,1
```

これは「ピンク色の標準パーティクルを32個、半径1.5、速度係数0.7」で表示する試作です。最後の`1`は**プレビュー専用**の指定なので、既存の抽選確率を変えずに試せます。

保存 → ゲームを閉じる → Develop.cmd → F1のオーラ一覧から **My Test Aura** を選びます。選択するとAutoRollが停止し、所持数や履歴は増えません。

変更するのは元の `data/auras.csv` です。ビルド時に `build-release/data` へコピーします。出力先のコピーを編集すると次回のコピーで上書きされます。

| CSVの列 | 意味 |
| --- | --- |
| `id` | セーブで使う識別名。新作は新しいidにする。既存idの変更は別オーラ扱いになる |
| `name` | 英語表示名。独自idに日本語訳がなければ日本語画面でもこの名前を表示 |
| `denominator` | 通常抽選の分母。4なら1/4。試作は0を指定 |
| `red,green,blue` | 色。各0〜1 |
| `particles,radius,speed` | 標準の粒子数・半径・回転速度 |
| `preview_only` | 1はF1表示だけ。0にするのは抽選確率を正式に決めるとき |

特殊オーラの大きな生物や玉などはCSVの標準粒子ではなく、専用の `src/tuning/*.hpp` で調整します。既存idの日本語名は `src/core/presentation.cpp` の対応表が優先されます。

通常抽選へ追加するときは、すべての確率の合計を考える必要があります。詳しい条件は [aura-format.md](aura-format.md) にあります。最初は上のプレビュー行から始めると、確率計算を触らず絵作りに集中できます。

Pixelation用の2曲はWindows版ZIPの`data/audio`からソース側の`data/audio`へコピーして利用できます。VRMは自分のPCのファイルをF1から指定します。ソースZIPにはモデルや参考動画を含めていません。

## 7. 変わらない・ビルドできないとき

| 状況 | 確認すること |
| --- | --- |
| 保存したのに変わらない | Ctrl+Sしたか、Develop.cmdで作り直したか、古いEXEを開いていないか |
| Nanallyの設定を変えたのに出ない | F1からNanallyを選ぶ。プレビューなのでロールでは出ない |
| ポーズの設定を変えたのに違いがない | 対象オーラと同じレベルで見比べる。歩行用と固定ポーズ用は別 |
| ファイルを上書きできない | ゲームを閉じてから再ビルド |
| C++のエラーが出る | 最初のエラーを見る。変更した1行を元に戻し、`;`や`,`や`f`を確認 |
| `static_assert`と出る | 設定が指定範囲外。直前のコメントとチェックの範囲へ戻す |
| ツールがないと言われる | Visual Studio InstallerでMSVC x64、Windows SDK、CMakeを確認 |
| F1で変えた色や言語が戻らない | F1設定は保存する仕組み。パネルで変更するか、ゲームを閉じてlocal-save/settings.txtを別名へ退避して初期化 |

変更前の数字をコメントに残すか、プロジェクトを別フォルダーへコピーしておくと戻しやすくなります。複数を同時に変えるより、まず1つずつ見比べてください。

## 8. 処理を追いたくなったときの地図

| ファイル・関数 | 何をしている？ |
| --- | --- |
| `src/platform/main.cpp` の `run()` | 入力の確認・時間更新・描画を繰り返すゲーム全体のループ |
| 同じファイルの `roll()` | 抽選 → 所持数更新 → CSVログ → セーブ。表示中のオーラは維持 |
| 同じファイルの `update()` | 経過時間、移動、AutoRoll、BGMなどを進める |
| 同じファイルの `applyDebugActions()` | F1から届いた変更をゲーム状態へ反映 |
| `src/core/aura.cpp` | CSV読込、チケット抽選、Luck、進行データの保存復元 |
| `src/core/presentation.cpp` | 表示用のプレビュー、訳名、設定の保存復元 |
| `src/core/animation.cpp` | 手足の姿勢、弾が衝突する位置などの計算 |
| `src/render/renderer.cpp` の `world()` | ステージ → アバター → オーラを組み立て、GPUへ渡す |
| `src/render/nanally.cpp` など | それぞれの特殊オーラの形・発光・粒子を作る |
| `src/render/ui.cpp` の `debugUi()` | F1のImGuiパネル。命令を作り、main.cppに処理を依頼する |
| `src/platform/audio.cpp` / `discord.cpp` | 音声の再生 / Discordとの通信 |
| `third_party` | 同梱ライブラリの元コード。ゲーム独自の改造はsrcから始める |

描画ファイルの `R"(... )"` 内はHLSLというGPU用コードです。数学・描画資源の管理を詳しく変更したくなったときに読む場所として、各ファイルの入口に説明を付けています。普段の大きさ・色・量・速度の変更は、まず `src/tuning` だけで試してみてください。
