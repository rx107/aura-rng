# フレームワークと拡張

## 入力から画面まで

Win32入力 → Appが操作を検証 → Rollerで抽選 → Progressで所持数・総回数を更新 → CSVとセーブを保存 → FrameをRendererへ渡して描画します。

`src/core` はDirectXから独立した計算です。抽選、進行、設定、共通ポーズ、惑星移動、低音の計測をテストできます。`Renderer` は読み取り用Frameから描くだけで、UIはHit / DebugActionsを返し、Appが変更を反映します。

`Progress::record()` は装備を変えません。実装備はCollection操作から変更します。`Presentation::preview` は未所持でも確認できる一時表示です。プレビューはAutoRollを止め、在庫・履歴・保存中の装備を変えません。

## ステージと抽選

`Biome` は既存番号を保ち、Extraを末尾に追加した6種類です。`Roller::setBiome()` が惑星ごとの有効な行を選び、`setLuck()` がCDFをL乗した分布を計算します。対象外の行はLuckを上げてもチケット0枚です。

`auraCategory()` と `availableInBiome()` が分類と取得条件の入口です。Normal13種類は全惑星、Desert3種類は砂漠、Extra4種類はExtra限定。AdministratorのNanallyだけ `preview_only=1` で、抽選・在庫・履歴へ登録できません。計21行、取得可能20種類です。CSVの指定分母を維持し、各惑星で残った確率をStardustへ配分します。

`StageTravel` は移動の時計です。選択中・移動中は入力とAutoRollを止め、宇宙で画面が覆われる中間点に行き先を交換して保存します。Extraの丸い星形は `extraStageRadius()` を描画と歩行制限で共有します。各ステージの背景物との物理衝突はありません。

## ポーズと描画

`animation.cpp` がブロックとVRMで共通の姿勢を計算します。通常は歩行、Singularity・Pixelation・Equinox・BreakThrough・Illusionaryは固定ポーズ。Illusionaryは胴中心を支点に横向きへ倒し、片脚を曲げます。固定中は移動量で手足を振りません。

共有キューブ、専用クアッド、インスタンス描画で描画回数を抑えます。特殊オーラの光だけHDRブルームを通します。Illusionaryの黒い帯、チェック床、文字は不透明バッチへ分け、床は深度を書き込み、文字はブルームを通しません。

VRMはcgltfとnlohmann/jsonで自己完結GLBを読み、別スレッドで初期モーフ、スキン、テクスチャを準備します。DirectXの即時コンテキストはメインスレッド専用です。GPUスキニングで描画し、失敗時はブロックを表示します。モデルはローカル選択で、アップロードや再配布をしません。

## CutSceneと音声

CutSceneの映像はGPUシェーダーとD2D字幕で描き、動画を貼り付けません。同梱M4Aは演出の音声範囲だけ切り出したものです。元MP4は同梱せず、同梱音声がない場合のDownloadsフォールバックとしてだけ使います。

`Music::playbackSeconds()` のXAudio2出力サンプル数を映像の時計に使い、初回デコード待ちで音と映像が離れないようにします。再生末尾でSamplesPlayedが0へ戻っても終了時刻を保持し、CutSceneが最初へ戻らないようにしています。

音声の読み込み時に `bassEnvelope()` が35〜180Hz付近の強さを10ms間隔で計測します。毎フレームは小さな表を補間するだけです。Equinoxの背景と字幕を同じ量だけ上下し、音声がなければ揺れは0です。

`endingOpacity()` が最後0.8秒の映像透明度と音量を制御します。通常オーラを先に描き、CutSceneを標準アルファ合成で重ねます。自然終了は対応オーラの一時表示を残し、Esc/停止は直前のプレビューへ戻します。取得時は実際のロールだけを在庫へ記録し、演出再生は記録しません。

MP3 / M4A / WAVはWindows Media Foundation、Ogg Vorbisは同梱stb_vorbisでPCM16へ変換し、XAudio2で再生します。Pixelationの発射・衝突音は短いオリジナルPCMを合成します。

## 保存と設定

進行schema 2は編集可能な `rolls` と実在庫合計 `collected` を分けます。所持数は安定したIDで保存し、削除したIDも引退在庫として保持します。旧schema 1、旧カタログ、旧プレビューから抽選対象へ変わったオーラも移行できます。

設定schema 6は言語・服・Luck・惑星・VRM/BGMパス・音量・プロフィール・Discord・字幕時刻を保存し、schema 1〜5も読みます。総回数のF1変更は在庫や既存CSVを変えません。プレビューとデバッグLevelは保存しません。

Discordはローカル名前付きパイプを専用ワーカーで扱い、ユーザー名・総回数をRich Presenceへ送ります。公開Application IDだけを用い、トークンは扱いません。実Discordのプロフィール表示は未検証です。[連携の説明](discord.md)

## 改造の入口

数値は[調整ファイル一覧](../src/tuning/README.md)、CSVは[データ仕様](aura-format.md)、将来のマルチプレイヤー・実績・お宝は[必要な準備](next-features.md)へ。これらの将来機能は今回は実装していません。
