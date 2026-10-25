# フレームワークと拡張

## 入力から画面まで

Win32入力 → Appが操作を検証 → Rollerで抽選 → Progressで所持数・総回数を更新 → CSVとセーブを保存 → FrameをRendererへ渡して描画します。

`src/core` はDirectXから独立した計算です。抽選、進行、設定、共通ポーズ、惑星移動、低音と拍の計算をテストできます。`Renderer` は読み取り用Frameから描くだけで、UIはHit / DebugActionsを返し、Appが変更を反映します。v0.1.16では、この流れへマルチプレイの読み取り用スナップショットも追加しています。

`Progress::record()` は装備を変えません。実装備はCollection操作から変更します。`Presentation::preview` は未所持でも確認できる一時表示です。プレビューはAutoRollを止め、在庫・履歴・保存中の装備を変えません。

## ステージと抽選

`Biome` は既存番号を保ち、Extraを末尾に追加した6種類です。`Roller::setBiome()` が惑星ごとの有効な行を選び、`setLuck()` がCDFをL乗した分布を計算します。対象外の行はLuckを上げてもチケット0枚です。

`auraCategory()` と `availableInBiome()` が分類と取得条件の入口です。Normal13種類は全惑星、Desert3種類は砂漠、Extra6種類はExtra限定。Frutiger Aeroは1 in 5,000,000、Red johnは1 in 6,000,000が初期の基礎確率です。AdministratorのNanallyだけ `preview_only=1` で、抽選・在庫・履歴へ登録できません。計23行、取得可能22種類です。CSVの指定分母を維持し、各惑星で残った確率をStardustへ配分します。

`StageTravel` は移動の時計です。選択中・移動中は入力とAutoRollを止め、宇宙で画面が覆われる中間点に行き先を交換して保存します。Extraの丸い星形は `extraStageRadius()` を描画と歩行制限で共有します。各ステージの背景物との物理衝突はありません。

## ポーズと描画

`animation.cpp` がブロックとVRMで共通の姿勢を計算します。通常は歩行、Singularity・Pixelation・Equinox・BreakThrough・Illusionary・Red johnは固定ポーズ。Illusionaryは全身の傾きに胸の反りと首の反りを重ね、真上を仰ぎます。肩と肘も別に曲げ、一方の腕だけを直角にして前へ出します。Red johnは片膝を立てて片腕を乗せ、反対の手でベッドを支えます。腕と脚のYawで横へ折り、VRMの腰は座る基準高さへ揃えます。固定中は移動量で手足を振りません。

Pixelationの`pixelationPlayer()`は、現在のPLAYER1/2・弾の発射位置・光の尾で共有する3D軌道です。描画側だけで上下位置を変えないため、機体から弾が離れて発射されるずれを防ぎます。発射時の周回時計を固定し、飛行中に機体が移動したりLevelを変更したりしても、弾の出発点を保ちます。2発は同じAction時計を使って共通の衝突点へ到着します。機体や周囲のMotion時計とは分離しています。

共有キューブ、専用クアッド、インスタンス描画で描画回数を抑えます。特殊オーラの光だけHDRブルームを通します。Illusionaryの黒い帯、チェック床、文字は不透明バッチへ分け、床は深度を書き込み、文字はブルームを通しません。

EquinoxはBGMの再生時刻と保存したBPM・時差から共通の拍数を求めます。標準の周回は4拍、白黒交換は1拍、床文字の交換は4拍です。床文字は「陰→陽→衡→無」の16拍ループ。二つの玉は逆の白黒を保ち、交換した瞬間の拍内の経過量からリングと破片を描きます。トレイルは実際の過去の玉の座標を結ぶため、玉から離れた波形や点列になりません。通常の名前の白黒も同じ拍を使います。

v0.1.16の玉は半径1.80、中心の高さ2.70、周回半径3.60です。`orbOrbitAngle()`が音楽の拍から加速・減速を含む角度を求め、玉・過去のトレイル・交換時の爆発位置が同じ`orbPosition()`を使います。`orbitSpeedVariation=.22f`では平均の78〜122%で動き、0なら等速。強さを1未満に制限するため逆走せず、1周期の平均テンポを保ちます。`floorRotation=1.10f`は床の面・輪・粒子で共有し、輪の上下の波も回転前の角度に固定して模様ごと回します。爆発は広がり1.65、光量.82、破片18個です。すべて`src/tuning/equinox.hpp`に日本語の調整例があります。

Pharaohはピラミッドと二重の床輪、左右の長短二段の羽、青緑の宝石、二重の太陽を既存の発光板で描きます。標準値で最大437枚、Duneは144枚、Mirageは142枚です。専用画像や新しい描画パスを増やさず、線幅を広げて形を出し、不透明度を抑えて白飛びを防ぎます。

Frutiger Aeroは`src/render/frutiger.cpp`で草原・小川・青空・雲・透明な泡・青い蝶を描きます。草の揺れ、雲の流れ、羽の回転は別々の調整値を使います。虹色の薄膜と白い映り込みでガラスの光沢を作り、透明な板は遠い順に並べます。参考画像を貼り付けず、GPUの数式で模様を作るため、元画像の同梱は不要です。[見た目の調整例](frutiger-aero.md)

Red johnは`src/render/redjohn.cpp`で部屋・ベッド・枕・窓・赤い顔を既存の立体バッチへ追加します。壁の顔は輪・目・口・垂れるペンキを線で組み立て、写真は読み込みません。カメラ側の壁を省くドールハウス表示で中を見せ、家具や壁はアバターの位置・向きと一緒に動きます。調整値は`src/tuning/redjohn.hpp`、座る角度は`src/tuning/avatar.hpp`の`redJohn`です。[変更の手順](red-john-guide.md)

VRMはcgltfとnlohmann/jsonで自己完結GLBを読み、別スレッドで初期モーフ、スキン、テクスチャを準備します。DirectXの即時コンテキストはメインスレッド専用です。GPUスキニングで描画し、失敗時はブロックを表示します。モデルはローカル選択で、アップロードや再配布をしません。

## マルチプレイの境界

自分の操作 → `App`が`LocalPlayer`へ位置・向き・歩行・惑星・表示中のオーラとLevel・服色をまとめる → `Client::submit()` → WinHTTPの専用ワーカーが約100msごとに専用サーバーへ送る → `Snapshot`を描画へ渡す、という流れです。通信の完了を待ってゲームの描画を止めません。サーバー全体で自分を含む最大32人を扱います。ロール・所持品・Luckは各PC内で管理し、オンラインへ参加してもサーバー主体の抽選には切り替わりません。

UserName未設定なら参加前に名前を確定します。UserNameと別に初回生成する10桁のUserIDを持ち、名前を変えてもIDは維持します。IDと一緒に作る秘密鍵はWindowsのDPAPIで保護して`multiplayer_identity.json`へ保存します。このローカルの識別情報はプロフィール写真や設定とは別です。

写真の選択は`src/render/multiplayer_ui.cpp`、画像の準備は`src/platform/profile_photo.cpp`です。WindowsのWICでPNG/JPEG/BMPを読み、中央を正方形に切り取り、160×160のPNGとして保存・送信します。元の写真ファイル・ファイル名・メタデータは送信しません。届いた小さいPNGはGPUテクスチャとして再利用し、毎フレーム読み直しません。

`src/render/multiplayer.cpp`の`blockAvatar()`は自分と相手のブロックアバターで共有します。相手の位置は補間し、同じ惑星だけをワールドに表示します。近い4人までを詳細オーラの対象にし、それ以外は少数の星などで負荷を抑えます。他人のVRMは送受信せず、相手はブロックアバターです。送信するのは現在表示中のオーラとLevelで、F1の未所持プレビューも相手に見えます。プレビューによって所持数・実装備・Roll Historyを増やすことはありません。

同梱の`server/serve.py`は参加者・位置・セッションを管理するPython専用サーバーです。`server/stars-rng.service`がLinuxでの常駐・自動起動、`server/Caddyfile.example`が外部HTTPSからVPS内サーバーへの転送を担当します。接続先は各自のホストのHTTPS URLを設定し、実運用ホスト名や秘密鍵は配布物へ含めません。手元の2人接続からXServer VPS設定へ進む手順は[マルチプレイのガイド](multiplayer.md)にまとめています。Red johnの追加でも通信はprotocol=1のままで、既存サーバーの更新は不要です。

## CutSceneと音声

CutSceneの映像はGPUシェーダーとD2D字幕で描き、動画を貼り付けません。Equinox / BreakThroughの同梱M4Aは演出の音声範囲だけ切り出したものです。v0.1.19のRed johnの`Red_John_Cutscene.m4a`は、提供されたMP4の0秒から約10.89秒のAAC音声を再圧縮せず取り出したもので、元の音質を保ち、提供者の許可を得て配布します。元MP4は同梱せず、同梱音声がない場合のDownloadsフォールバックとしてだけ使います。

Red johnの映像は`src/render/redjohn_cutscene.cpp`です。GPUで横切る帯と粒状感を描き、Direct2Dで目・角のあるアバターの横顔・大きなRED JOHNの文字・赤い顔を重ねます。`src/tuning/redjohn.hpp`で約10.9秒の時間軸を共有します。v0.1.19では音声だけを差し替え、映像やその時刻は維持します。F1の再生でも実際の入手でも同じ演出を使います。`tools/generate_redjohn_audio.py`は旧合成曲用の任意ツールで、現在の音声の再生成や通常のビルドには使いません。

`Music::playbackSeconds()` のXAudio2出力サンプル数を映像の時計に使い、初回デコード待ちで音と映像が離れないようにします。再生末尾でSamplesPlayedが0へ戻っても終了時刻を保持し、CutSceneが最初へ戻らないようにしています。

BreakThroughのカメラは`breakthroughCamera()`で計算します。i・iiは初実装v0.1.6の近接カメラを`BreakthroughOriginalI / BreakthroughOriginalII`として共有します。iiiはv0.1.13のコアへ加速して直進するカメラ、ivの11.1〜14.15秒は同版の広角・巨大な柱・閃光を維持します。iv字幕の13〜14.12秒だけは`breakthroughIvCaptionVisible()`に合わせて二値化し、白地と黒い構造物・光源の形で描きます。字幕は黒帯に白文字です。14.15〜15.05秒は`blackout`で完全な黒にし、`get out of my head`だけを表示します。15.05秒以降は初実装の7本柱の全景と終盤の絞り・閃光へ戻します。`it wasn't gone`と`with only 5 fragments left`は`breakthroughCaptionDrift()`で同じ横移動を使います。カメラ値は`src/tuning/cutscene.hpp`、GPU側の描画は`src/render/breakthrough.cpp`が担当します。

音声の読み込み時に `bassEnvelope()` が35〜180Hz付近の強さを10ms間隔で計測します。この低音表はPixelationのBPM推定などに使います。EquinoxのCutSceneの揺れは生の低音の大きさをそのまま使う方式から、保存したBPM・時差と実際の音声再生時刻から拍を刻む方式へ変えました。背景と字幕は同じ量だけ上下させます。通常BGMとCutSceneは曲が別なので、BPM・時差の設定も分けます。

Pixelationのキューブ用には、その低音表の立ち上がりの繰り返しを自己相関で調べ、`estimateBeatGrid()`がBPMと最初の拍の位置を推定します。先頭最大120秒を読み込み時に解析し、描画ごとに解析し直しません。推定は半分・倍のテンポを選ぶ場合があるため、F1で30〜300 BPMと-2〜+2秒の時差を上書きできます。`beatPulse()`は実際の再生サンプル時刻から0〜1の鼓動を作り、`Frame::auraBeat`で描画へ渡します。ループ曲は曲の長さで時刻を折り返し、非再生中は0にします。Motion時計の倍率を変えてもBPMは変わりません。

`endingOpacity(elapsed, duration, instantSwitch)` が映像透明度と音量を制御します。EquinoxとRed johnは最後0.8秒で下げ、通常オーラへ標準アルファ合成で重ねます。BreakThroughだけ第3引数を`true`にし、終端直前まで不透明・音量一定で、30秒になったら対応オーラへ即座に切り替えます。映像内では、冒頭0.25秒の導入、i・iiの絞り閉鎖と開口、iiiの冒頭の開口、終盤の絞りと閃光を保ちます。これらは通常画面へ重ねる終了フェードとは別の処理です。自然終了は対応オーラの一時表示を残し、Esc/停止は直前のプレビューへ戻します。取得時は実際のロールだけを在庫へ記録し、演出再生は記録しません。

MP3 / M4A / WAVはWindows Media Foundation、Ogg Vorbisは同梱stb_vorbisでPCM16へ変換し、XAudio2で再生します。Pixelationの発射・衝突音は短いオリジナルPCMを合成します。

公開する音源はRed johnの抽出音声を含む許可済みの計8ファイルです。Equinoxの通常表示用には148.24秒の`Koraii_Layers_Cut.mpeg`を使い、CutSceneは従来の10.9秒の`Equinox_Cutscene.m4a`を使います。通常の曲をCutSceneへ流用しません。元の参考動画・画像・ユーザーのVRMは配布しません。

## 保存と設定

進行schema 2は編集可能な `rolls` と実在庫合計 `collected` を分けます。所持数は安定したIDで保存し、削除したIDも引退在庫として保持します。旧schema 1、旧カタログ、旧プレビューから抽選対象へ変わったオーラも移行できます。

設定schema 9は従来の言語・服・Luck・惑星・VRM/BGM・音量・Discord・字幕・各曲のBPMと時差に加え、`usernameConfigured`、`serverEndpoint`、`profilePhotoPath`を保存します。ファイルの行名は`username_configured`、`server_endpoint`、`profile_photo`です。旧schema 1〜8も読み、旧UserNameが`Player`なら未確定、それ以外なら設定済みとして移行します。新しい接続先は`http://127.0.0.1:8765`、写真は未選択が初期値です。写真のパスは変換後のローカルPNGを指します。IDと秘密鍵は設定ファイルに混ぜません。

schema 6以前ではPixelationのBPMが自動推定になり、schema 7以前ではEquinoxが同梱曲に合わせた初期値で始まります。schema 8からの移行は保存済みの曲・BPM・時差を保ちます。Equinoxの手動BPMは30〜300、時差は±2秒に制限します。総回数のF1変更は在庫や既存CSVを変えません。プレビューとデバッグLevelは保存しません。

Discordはローカル名前付きパイプを専用ワーカーで扱い、ユーザー名・総回数をRich Presenceへ送ります。公開Application IDだけを用い、トークンは扱いません。実Discordのプロフィール表示は未検証です。[連携の説明](discord.md)

## 改造の入口

数値は[調整ファイル一覧](../src/tuning/README.md)、CSVは[データ仕様](aura-format.md)、通信とVPSは[マルチプレイ](multiplayer.md)へ。次のサーバー主体の進行管理・実績・お宝は[必要な準備](next-features.md)へ。実績とお宝はまだ実装していません。
