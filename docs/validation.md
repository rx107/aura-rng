# Star's RNG Alpha 0.1 / v0.1.5 検証記録

検証日: 2026-10-16（日本時間）。Windows x64、Visual C++ 19.39、Windows SDK 10.0.22621.0、Releaseビルド。

## コアテスト

- 20万回のシード再現と分布確認。Commonの実確率が正確に50%であることを検証。
- 全13種類の実チケット数が異なること、全区間の先頭・末尾、合計10^12枚を検証。Uncommonは正確に25%、Stardustは残り6.9888989%。
- PixelationとEquinoxはプレビュー専用で0チケット。Luck 1.5 / 2 / 10 / 1000 / 100万倍でも抽選対象外で、既存13種類の基礎確率・Luck適用後の配分は追加前と同一。
- 旧9列CSVを読み込めること、追加の`preview_only`列では0 / 1以外を拒否することを確認。
- ロールで装備が変更されないことを検証。
- 未所持プレビューは装備・所持数・ロール数を変更せず、自動ロールを停止。プレビュー中のロールでも表示が固定されることを検証。
- セーブの上書き・復元・ID並べ替え・削除したIDの再追加、破損検出。
- 旧12種・13種・14項目のセーブからSingularityの所持数・装備・Aura Levelを保持し、PixelationとEquinoxを所持数0で追加。
- Pixelationの通常取得・ログ登録を拒否し、拒否後の所持数・CSVは不変。不正な非0所持数を含むセーブのロード、試作オーラの装備・best・非0所持数の保存を拒否。
- 試作オーラをカタログの先頭へ移動しても、初期装備とbestは通常オーラから選ぶことを確認。Commonの旧IDが存在しない場合や、旧ID自体が試作専用の場合も確認。
- 日本語・カンマ・引用符を含むCSVログ。
- 言語と服のRGB色の保存復元、範囲外値・NaNの拒否。
- 星が左から右へ正弦波上を移動し、開始・終端でフェード、時間0でバーが消えることを検証。
- Luck 1.5 / 2 / 10 / 1000 / 100万倍でチケット合計を維持。2倍でCommonが25%、Singularityがほぼ2倍。1倍に戻すと元の配分へ完全復元。NaN・範囲外の拒否。
- Luck、アバター、バイオーム、音量、3曲のパス、日本語・空白・引用符入りのファイルパスの保存復元。旧schema 1 / 2からschema 3への移行。
- Singularityの初取得レベル0、重複で1〜3、上限3、所持数の保存復元、プレビューのレベルによる所持数不変を確認。
- Pixelationのレベル0〜3がSingularityの設定に影響せず、履歴・所持数を変更しないことを確認。重複時の計算規則と上限3を検証し、現行のプレビュー専用設定では実取得できないことも確認。
- Pixelationのレベル1〜3は共有モーション時計が1.6倍速。20周期分の球の発射点、同時到達する共通衝突点、飛行中も継続するPlayer軌道、発射・衝突のcycle IDを検証。
- Equinoxの固定浮遊ポーズの有限値と、カットシーンの共通尺10.9秒を確認。
- 固定ポーズが歩行位相と移動量に影響されないこと、通常の歩行で左右の脚が交互に動くことを確認。
- レベル3の星がブラックホールへ近づき、縮小する軌道を確認。

## Windows描画・入力テスト

NVIDIA GPUとWARPの両方で次を確認しています。最終の衝突点の高さ・頭上ラベル位置・Equinoxの衡文字の白黒反転の調整後はNVIDIA GPUで再検証しました。WARPの記録は、この見た目の調整前の同じ描画・入力経路による結果です。

- Direct3D 11、Direct2D、Dear ImGui Win32 / DX11バックエンドの初期化・描画・終了。
- ボタンクリック、Space、F、Wの入力経路と1000×700 / 1440×900のリサイズ。
- 全13抽選オーラとPixelation・Equinoxの描画。頭上には名前・`1 in N`を表示し、試作2種類は`Preview only`を表示。
- 通常・自動ロールでもコレクションの装備を維持。
- デバッグの未所持プレビューがCSVのファイルサイズも含めて履歴を変更せず、自動ロールを停止。
- Pixelationへの切り替えでAutoRollが停止し、所持数・装備・履歴・CSVサイズを変更しないことを確認。プレビュー中の通常ロールでも表示を維持し、抽選対象外のPixelationを所持数へ追加しない。
- Pixelationの24フレームを出力し、色の循環、キューブ・リング・ピクセル・HUDの動きを確認。1000×700から1440×900へのリサイズ、別オーラへ切り替えた後に専用演出が残らないことを検証。
- PixelationのLevel 0〜3、1.6倍の演出速度、両球が同時に同じ場所で衝突することを確認。最終表示の衝突点はアバター相対Y=3.60の頭上です。制御した1周期で発射音・衝突音がそれぞれ1回となり、プレビュー・レベル変更後に過去の音をまとめて鳴らさないことを検証。
- Equinoxの通常表示、別オーラとの切り替え、履歴・装備・所持数の不変を確認。
- Equinoxカットシーンの入力制御、AutoRoll停止、リサイズ、60フレームの連続描画、10.9秒での終了、Escとデバッグ停止、元のプレビュー・レベル・デバッグ画面への復元を検証。
- コレクションで装備するとプレビューが解除され、装備が保存されること。
- 英語、日本語、赤い服、ImGuiパネル、クールタイム中・完了後の画面をPNG出力し、目視確認。
- 右ドラッグの開始・上下左右の移動・デバッグ画面上までの継続・ボタン解放・キャプチャ喪失での終了。パネル開閉の両方でテスト。
- ImGuiのコマンドからLuckと通常 / 砂漠を切り替え、各景観・Singularityの浮遊ポーズを描画。
- 独自の24頂点・10ボーンのVRMテストモデルでWARP上のGLB・初期モーフ・埋め込みPNG・GPUスキニング・歩行・固定ポーズ・再読み込み・不在時のブロック表示を検証。
- 添付VRM 0.xモデルで124,172頂点・22プリミティブの読み込み、腕を下ろした立ち姿、衣服を含む歩行を確認。脚の骨の座標変化も検証。Singularity・Pixelation・Equinoxの固定ポーズの骨座標は移動中も不変。独自VRMのWARPテストでも3種の固定ポーズを確認。モデルとその画像は公開リポジトリに含めていません。
- 添付MP3を11,061,696バイトのPCMにデコードし、XAudio2でSingularity表示時のループ開始・他オーラへの変更による停止・再選択での再生を検証。オーラ・景観・音声の設定で履歴が増えないことも確認。
- NVIDIA GPUの実行でRedline Shiftを11,550,144バイト、Censoredを41,290,176バイトのPCMにデコード。Pixelation Level 1〜2のループ再生、Level 3での曲の切り替え、表示変更後の停止を検証。
- ローカルの参考MP4から音声ストリームを2,924,544バイトのPCMへデコードし、カットシーン開始時に先頭から再生、終了・スキップ時に即停止することをGPU・WARPで検証。元動画の後半のコレクション映像の音声へ進まないことを確認。元動画がない場合の映像のみの経路も実装。
- Aura Level 0〜3で星・ブラックホールの有無が変わることと、デバッグ操作で履歴が増えないことを確認。
- 拡張ステージの旧境界より外（X=12）でも移動できることを確認。ステージは64×64単位。

## ローカル測定

| 条件 | 値 |
| --- | --- |
| EXE | 1,490,944バイト（約1.42MiB、ImGui・パーサー・VC++ランタイムを静的リンク） |
| NVIDIA GeForce RTX 3050 6GB Laptop GPU / 通常ブロック | 平均フレーム送信時間 1.24554ms |
| 同GPU / Pixelation / ブロック | 2.98197ms |
| 同GPU / Equinox / ブロック | 2.81075ms |
| 同GPU / カットシーン | 0.80195ms |
| 同GPU / 添付124,172頂点VRM | 2.98632ms、アバターで22 draw callsを追加 |
| Microsoft Basic Render Driver / WARP / 通常ブロック | 2.76075ms |
| WARP / Pixelation / ブロック | 6.67084ms |
| WARP / Equinox / ブロック | 5.97956ms |
| WARP / カットシーン | 1.58987ms |
| WARP / 独自24頂点VRM | 5.79416ms、アバターで1 draw callを追加 |
| 共有キューブのワールド描画 | 1 draw call（オーラや景観でインスタンス数が変動） |
| Pixelationの演出 | 1213インスタンスを1バッチ |
| Equinoxの演出 | 発光と専用マテリアルの四角形を合わせて1016インスタンス |
| カットシーン | 全画面三角形1枚のシェーダー描画と字幕UI |
| Pixelation表示時のブロック3D合計 | ワールド1 + 演出1 + ブルームぼかし2 + 合成1 = 5 draw calls |
| Pixelationのブルーム用ターゲット | 1440×900で約7.4MiB、初回使用時に遅延確保してキャッシュ |

1440×900、VSyncなし、デバッグパネルを閉じた状態で表示せず送信した値です。通常・オーラ・VRMは60フレーム、カットシーンの性能測定は時刻1.5秒を固定した20フレームです。別途、カットシーンの連続描画60フレームも検証しています。独立したGPUタイマーの値や実プレイのFPS保証ではありません。通常起動はVSyncを有効にし、ImGuiパネル表示時にはUIの描画バッチが追加されます。

PixelationとEquinoxのブルームは全解像度1枚と半解像度2枚の`R11G11B10_FLOAT`テクスチャを共有します。標準オーラではこの後処理を実行しません。標準オーラの粒子上限は160個、Pixelationは専用の180ピクセル・20光点を使用し、Level 2以降は飛行・衝突の位相に応じて球や衝突光も追加します。

## 再現

```bat
ctest --test-dir build-release --output-on-failure
build-release\StarRng.exe --smoke-test --seed=42 --save-dir=C:\Temp\star-test-save --capture=C:\Temp\star-preview.png --report=C:\Temp\star-report.txt
build-release\StarRng.exe --smoke-test --warp --seed=42 --save-dir=C:\Temp\star-warp-save --capture=C:\Temp\star-warp.png --report=C:\Temp\star-warp-report.txt
```

出力先はテスト用の書き込み可能なフォルダーへ変更してください。`--smoke-test`では保存先の明示が必要で、通常のセーブに触れません。キャプチャ指定時は`*-small.png`、`*-cooldown.png`、`*-japanese.png`、`*-debug.png`、`*-complete.png`も出力します。GUIのコマンド処理と描画はスモークテストで検証し、全てのOS入力デバイス操作を自動化するものではありません。

追加で`*-desert.png`と`*-space.png`を生成します。`--vrm="C:\path\avatar.vrm"`を指定するとVRMの読み込み・切り替え・欠落ファイルのテストと`*-vrm.png`を追加します。`--bgm="C:\path\music.mp3"`を指定すると実際の再生・停止も確認します（音声出力デバイスが必要）。通常起動でもこれらの引数でローカルファイルを指定できます。

各`*-level0.png`〜`*-level3.png`、`*-vrm-walk-a.png` / `-b.png`、`*-vrm-space.png`も生成します。VRM指定時は歩行24フレームの画像を追加し、衣服の追従も目視確認しています。同梱BGMへの切り替え後の再生も検証します。

Pixelationは`*-pixelation.png`と`*-pixelation-frame-0.png`〜`*-pixelation-frame-23.png`を生成します。抽選やプレビューの状態確認に加え、連続画像で色と動きを確認できます。VRM指定時は`*-vrm-pixelation.png`を生成し、固定ポーズも検証します。

Level 0〜3は`*-pixelation-level0.png`〜`*-pixelation-level3.png`、打ち合いは`*-pixelation-before-impact.png` / `-impact.png` / `-after-impact.png`と`*-volley-frame-*.png`を出力します。Equinoxは`*-equinox.png`と連続フレーム、カットシーンは`*-cutscene-small.png`、各段階の画像、`*-cutscene-frame-*.png`を出力します。VRM指定時は`*-vrm-equinox.png`も生成します。

新2曲の検証は`--pixelation-bgm="C:\path\Redline_Shift.mp3"`と`--pixelation-final-bgm="C:\path\Censored.mp3.mpeg"`で明示できます。参考MP4の音声検証には、Downloadsの`EquinoxReworkedCutscene.mp4.mp4`を使用します。新2曲は公開ZIPへ同梱し、GitHubソースには含めません。参考動画とVRMはソース・公開ZIPへ含めていません。

VRM 1.0も読み込みとプロシージャルアニメーション経路を実装していますが、今回の実モデル検証はVRM 0.xです。MToon、Spring Bone、表情は検証対象外・未実装です。

GitHub ActionsはWindowsビルド・WARP描画テスト・コアテスト、Ubuntuのコアテストを実行します。
