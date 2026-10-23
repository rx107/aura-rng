# はじめての改造ガイド

まずは「数字を1つ変えて、画面が変わる」を体験しましょう。C++を全部理解してから始める必要はありません。

## 1. 改造用のソースを用意する

[改造用ソースZIP](https://github.com/rx107/aura-rng/releases/download/v0.1.17-alpha.1/StarRng-v0.1.17-source.zip)をダウンロードし、右クリック → **すべて展開**します。ZIPの中から直接実行せず、展開先を開いてください。

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
| PixelationのPLAYERを速くする | 同上 → `PlayerOrbitSpeed` | `1.55` → `1.8`。2機の周回と尾だけが速くなり、弾がぶつかる間隔は変わらない |
| PLAYERの上下移動を弱める | 同上 → `PlayerBobAmplitude` | `.85` → `.4`。軌道が平らに近づく。`0`なら水平になる |
| PLAYERの光の尾を長くする | 同上 → `PlayerTrailDuration` | `.72f` → `1.f`。過去の軌道を長く残す。`PlayerTrailSegments`はカーブの滑らかさ |
| キューブの鼓動を大きくする | 同上 → `CageBeatScale` | `.15f` → `.22f`。1拍で膨らむ最大量が15%から22%へ。拍の速さは変わらない |
| キューブの拍を曲へ合わせる | F1 → 音楽 → キューブの鼓動 | 自動推定を外してBPMを指定。時差`+.10`で0.1秒遅らせる。再ビルド不要 |
| Equinoxの床文字を小さくする | `src/tuning/equinox.hpp` → `floorGlyphSize` | `3.2f` → `2.8f` |
| Equinoxの玉を体から離す | 同上 → `orbOrbitRadius` | `3.6f` → `4.f`。二つの玉の周回範囲が外へ広がる |
| Equinoxの玉を小さくする | 同上 → `orbRadius` | `1.8f` → `1.5f`。周回位置を変えずに玉だけ縮める |
| Equinoxの玉を上へ戻す | 同上 → `orbHeight` | `2.7f` → `3.1f`。玉の中心が0.4だけ上へ移動する |
| Equinoxの加速・減速を弱める | 同上 → `orbitSpeedVariation` | `.22f` → `.10f`。平均の90〜110%で動く。`0.f`ならずっと等速 |
| Equinoxの強弱をゆっくり変える | 同上 → `orbitSpeedCycleOrbits` | `1.f` → `2.f`。2周かけて加速・減速を1回行う。平均の周回速度は維持 |
| Equinoxの床と粒子を遅く回す | 同上 → `floorRotation` | `1.10f` → `.55f`。床の模様・円・粒子の回転が一緒に半分になる |
| Equinoxの尾を長くする | 同上 → `trailBeats` | `.80f` → `1.2f`。過去1.2拍分の軌道を残す。`trailWidth`なら太さを変更 |
| Equinoxの色交換の爆発を小さくする | 同上 → `burstRadius` | `1.65f` → `1.f`。リングと破片が広がる距離を抑える。明るさは`burstBrightness=.82f`で別に変更 |
| Equinoxの拍を曲へ合わせる | F1 → 音楽 → Equinoxの通常表示 / CutScene | BPMで拍の間隔、時差`+.10`で0.1秒遅くする。二つの曲を別々に保存 |
| BreakThroughの名前を上げる | `src/tuning/breakthrough.hpp` → `titleHeight` | `3.85f` → `4.1f`。名前と確率が上へ移動する |
| Nanallyの電撃を太くする | `src/tuning/nanally.hpp` → `ElectricWidth` | `.12f` → `.18f`。小さな稲妻の線が太くなる |
| Illusionaryの回転をゆっくりにする | `src/tuning/illusionary.hpp` → `MotionSpeed` | `1.f` → `.7f`。リボンと放射光の動きがゆっくりになる |
| Illusionaryの胸の反りを弱める | `src/tuning/avatar.hpp` → `illusionary::TorsoPitch` | `.55f` → `.35f`。腰より上を起こす。首だけなら`HeadPitch`を調整 |
| 砂漠オーラの砂を広げる | `src/tuning/desert.hpp` → `DuneRadius` | `1.75f` → `2.2f`。砂のらせんが外側へ広がる |
| Pharaohの金の線を太くする | 同上 → `PharaohPyramidLineWidth` | `.30f` → `.38f`。ピラミッドの辺が太くなる。光の色や明るさは変えない |
| Pharaohの翼を横へ広げる | 同上 → `PharaohWingWidth` | `3.45f` → `4.f`。左右の長い羽と短い羽が一緒に広がる |
| Frutiger Aeroの草原を広げる | `src/tuning/frutiger.hpp` → `MeadowRadius` | `4.7f` → `5.5f`。芝生と小川が一緒に広がる |
| Frutiger Aeroの泡を減らす | 同上 → `BubbleCount` | `22` → `12`。周りを飛ぶ泡が減る。大きい目印の泡2つは残る |
| Frutiger Aeroの蝶を大きくする | 同上 → `ButterflySize` | `.62f` → `.85f`。青い羽と模様が見やすくなる |
| Frutiger Aeroの雲を遅くする | 同上 → `CloudSpeed` | `.025f` → `.0125f`。雲が半分の速さで流れる |
| ロケットの乗降空間を高くする | `src/tuning/stages.hpp` → `RocketBoardingClearance` | `3.f` → `4.f`。機体だけ1単位上へ移動。発着地点は中央のまま |

`HalfExtent`は全幅の**半分**です。32なら全幅64。処理を軽く保つため、範囲チェックでは12〜48を指定できるようにしています。

Pixelationには時計が2本あります。**Motion**は周囲の粒子・立方体・PLAYERの回転、**Action**は弾・衝突・爆発・アバター浮遊です。Lv2/3のActionは同じ設定を使うので、同じ速さを保ちます。キューブの鼓動だけは別で、BGMの再生時刻を使います。回転を速くしても曲のBPMは変えません。

**BPMは1分間の拍数**です。120なら0.5秒ごと、150なら0.4秒ごとに鼓動します。音楽の自動推定は、低音の立ち上がりの繰り返しを探すため、曲によって半分・倍の値を選ぶことがあります。F1から30〜300の手動BPMと-2〜+2秒の時差で合わせられます。正の時差で遅く、負の時差で早くなります。曲が再生されていない間は鼓動せず、BGMのないLevel 0も同様です。

Equinoxは通常表示とCutSceneの音楽が異なるため、F1 → 音楽でBPM・時差を**別々に**合わせます。玉の周回は4拍で1周、白黒交換と小さな爆発は1拍ごと、床文字は4拍ごとの「陰→陽→衡→無」です。たとえば`floorGlyphBeats = 4.f`を`8.f`にすると文字の切り替えだけ半分の速さになり、4文字の1巡は32拍になります。通常の曲を差し替えたときは、その曲のBPM・時差も調整してください。[拍と揺れの調整手順](stage-and-cutscene-guide.md#equinoxの拍と揺れを音楽へ合わせる)

玉の強弱はBPMを変えずに`orbitSpeedVariation`で調整します。初期値`.22f`は平均速度の78〜122%で、止まったり逆走したりしません。許可範囲は0以上1未満です。尾と爆発位置も同じ動きを使うため、強弱を変更しても玉から離れません。床の`floorRotation`はラジアン/秒で、`1.10f`なら約5.7秒に1周です。粒子の回転もこの値へ連動します。

Frutiger AeroはCollectionのExtraにあり、Extraステージ限定・基礎確率1 in 5,000,000です。v0.1.16のカタログは22種類で、Nanallyを除いた21種類を取得できます。草・空・雲・泡の反射・青い蝶を作る処理と詳しい変更例は[Frutiger Aeroのガイド](frutiger-aero.md)を参照してください。

BreakThroughの`breakthrough.hpp`は**通常表示用**です。柱の浮遊幅や位置を変えても、別の処理で描くCutSceneの柱は変更されません。CutSceneの構造物は静止し、周回・後退・急接近・前進するカメラは`cutscene.hpp`で調整します。iv字幕中だけの白黒表示は`BreakthroughIvInkThreshold = .35f`が白と黒の境界です。[場面とカメラの調整表](stage-and-cutscene-guide.md#breakthroughの場面とカメラ)を参照してください。

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

許可済みの7音源はソースZIPにも同梱しています。Equinoxの通常表示用の曲とCutScene用M4Aは別ファイルです。VRMは自分のPCのファイルをF1から指定します。ソースZIPにはモデルや参考動画を含めていません。

## 7. 変わらない・ビルドできないとき

| 状況 | 確認すること |
| --- | --- |
| 保存したのに変わらない | Ctrl+Sしたか、Develop.cmdで作り直したか、古いEXEを開いていないか |
| Nanallyの設定を変えたのに出ない | F1からNanallyを選ぶ。プレビューなのでロールでは出ない |
| ポーズの設定を変えたのに違いがない | 対象オーラと同じレベルで見比べる。歩行用と固定ポーズ用は別 |
| キューブが鼓動しない | PixelationのLevel 1〜3でBGMが再生されているか確認。自動推定が0なら手動BPMを指定。Level 0にはBGMがない |
| キューブの鼓動が半分・倍に感じる | F1 → 音楽から自動推定を外し、BPMを手動指定。少しのずれは拍の時差で調整 |
| Equinoxの拍が音楽と合わない | F1 → 音楽で通常表示とCutSceneのどちらを直すか選ぶ。BPMで間隔、時差で出る瞬間を合わせる |
| マルチプレイの参加ボタンが押せない | 画面右上のマルチプレイを開き、名前を入力して「ユーザー名を確定・保存」を押す |
| サーバーに接続できない | 接続先とサーバーの起動を確認。PC内テストは`http://127.0.0.1:8765`、友達と遊ぶVPSはHTTPSを使う。[接続手順](multiplayer.md) |
| ファイルを上書きできない | ゲームを閉じてから再ビルド |
| C++のエラーが出る | 最初のエラーを見る。変更した1行を元に戻し、`;`や`,`や`f`を確認 |
| `static_assert`と出る | 設定が指定範囲外。直前のコメントとチェックの範囲へ戻す |
| ツールがないと言われる | Visual Studio InstallerでMSVC x64、Windows SDK、CMakeを確認 |
| F1で変えた色や言語が戻らない | F1設定は保存する仕組み。パネルで変更するか、ゲームを閉じてlocal-save/settings.txtを別名へ退避して初期化 |

変更前の数字をコメントに残すか、プロジェクトを別フォルダーへコピーしておくと戻しやすくなります。複数を同時に変えるより、まず1つずつ見比べてください。

## マルチプレイを手元で試す

マルチプレイの基礎接続はv0.1.16で追加しました。[マルチプレイの手順](multiplayer.md)にPC内での試験とVPSへの配置方法があります。画面右上の**マルチプレイ**でUserNameを確定し、必要ならPNG/JPEG/BMPの写真を選び、ホストから教わったHTTPS接続先を入力します。接続成功後は画面が閉じて操作へ戻ります。プロフィールを開き直した時は「ゲームに戻る」かEscで閉じられ、F1ならデバッグへ切り替えられます。名前・写真・接続先は再ビルドせずに変更できます。

UserNameとUserIDは別です。たとえば名前を`Player A`から`星の旅人`へ変えても、最初に作った10桁のIDは変わりません。写真は中央を160×160の正方形にして共有し、元の写真は送信しません。相手はブロックアバターで表示します。同じ惑星へ移動するとワールドで会えます。

接続しても、抽選・所持品・Luckは自分のPCに保存します。今のサーバーが受け持つのは参加者と見た目の同期です。`local-save`などの保存フォルダー、`multiplayer_identity.json`、変換後の`profile.png`、サーバーの`server-data`は、自作オーラのソースと一緒に公開しないでください。VPSで起動するファイル、HTTPS、自動起動の説明は[サーバーのガイド](multiplayer.md)にあります。

## 8. 処理を追いたくなったときの地図

| ファイル・関数 | 何をしている？ |
| --- | --- |
| `src/platform/main.cpp` の `run()` | 入力の確認・時間更新・描画を繰り返すゲーム全体のループ |
| 同じファイルの `roll()` | 抽選 → 所持数更新 → CSVログ → セーブ。表示中のオーラは維持 |
| 同じファイルの `update()` | 経過時間、移動、AutoRoll、BGMなどを進める |
| 同じファイルの `applyDebugActions()` | F1から届いた変更をゲーム状態へ反映 |
| `src/core/aura.cpp` | CSV読込、チケット抽選、Luck、進行データの保存復元 |
| `src/core/presentation.cpp` | 表示用のプレビュー、訳名、設定の保存復元 |
| `src/core/animation.cpp` | 手足・胸・頭・肘の姿勢、PLAYERの軌道、弾が衝突する位置などの計算 |
| `src/core/audio_envelope.hpp` | 音声の低音を計測し、BPMの推定と拍の強さを計算。描画中は計算済みの表を使う |
| `src/core/equinox_animation.hpp` | 音楽の拍から、速度に強弱のある玉の位置・色交換・床文字を計算。尾と爆発も同じ位置計算を使う |
| `src/render/renderer.cpp` の `world()` | ステージ → アバター → オーラを組み立て、GPUへ渡す |
| `src/render/nanally.cpp` など | それぞれの特殊オーラの形・発光・粒子を作る |
| `src/render/frutiger.cpp` | 草原・空と雲・泡の反射・蝶を描く。大きさや数は`src/tuning/frutiger.hpp`で変更 |
| `src/render/multiplayer.cpp` の `blockAvatar()` | 自分と相手で共通のブロックアバターを組み立てる。近い人のオーラと相手の位置補間もここ |
| `src/render/multiplayer_ui.cpp` | 名前・写真・接続先の設定画面。通信済みの小さな写真をテクスチャとして再利用 |
| `src/platform/profile_photo.cpp` | WICで写真を読み、中央を160×160 PNGへ変換して保存 |
| `src/platform/multiplayer.cpp` | WinHTTPの専用ワーカー。約10回/秒の位置更新、IDと秘密鍵の保護、参加・切断を扱う |
| `server/serve.py` / `stars-rng.service` / `Caddyfile.example` | 専用サーバー本体 / Linuxで常時起動 / HTTPSを受けてサーバーへ転送 |
| `src/render/ui.cpp` の `debugUi()` | F1のImGuiパネル。命令を作り、main.cppに処理を依頼する |
| `src/platform/audio.cpp` / `discord.cpp` | 音声の再生 / Discordとの通信 |
| `third_party` | 同梱ライブラリの元コード。ゲーム独自の改造はsrcから始める |

描画ファイルの `R"(... )"` 内はHLSLというGPU用コードです。数学・描画資源の管理を詳しく変更したくなったときに読む場所として、各ファイルの入口に説明を付けています。普段の大きさ・色・量・速度の変更は、まず `src/tuning` だけで試してみてください。
