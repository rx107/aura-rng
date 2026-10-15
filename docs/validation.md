# Star's RNG Alpha 0.1 / v0.1.2 検証記録

検証日: 2026-10-15（日本時間）。Windows x64、Visual C++ 19.39、Windows SDK 10.0.22621.0、Releaseビルド。

## コアテスト

- 20万回のシード再現と分布確認。Commonの実確率が正確に50%であることを検証。
- 全12種類の実チケット数が異なること、全区間の先頭・末尾、合計10^12枚を検証。
- ロールで装備が変更されないことを検証。
- 未所持プレビューは装備・所持数・ロール数を変更せず、自動ロールを停止。プレビュー中のロールでも表示が固定されることを検証。
- セーブの上書き・復元・ID並べ替え・削除したIDの再追加、破損検出。
- 日本語・カンマ・引用符を含むCSVログ。
- 言語と服のRGB色の保存復元、範囲外値・NaNの拒否。
- 星が左から右へ正弦波上を移動し、開始・終端でフェード、時間0でバーが消えることを検証。
- Luck 1.5 / 2 / 10 / 1000 / 100万倍でチケット合計を維持。2倍でCommonが25%、Singularityがほぼ2倍。1倍に戻すと元の配分へ完全復元。NaN・範囲外の拒否。
- Luck、アバター、バイオーム、音量、日本語・空白入りのファイルパスの保存復元。旧schema 1からの移行。

## Windows描画・入力テスト

NVIDIA GPUとWARPの両方で次を確認しています。

- Direct3D 11、Direct2D、Dear ImGui Win32 / DX11バックエンドの初期化・描画・終了。
- ボタンクリック、Space、F、Wの入力経路と1000×700 / 1440×900のリサイズ。
- 全12オーラの描画。
- 通常・自動ロールでもコレクションの装備を維持。
- デバッグの未所持プレビューがCSVのファイルサイズも含めて履歴を変更せず、自動ロールを停止。
- コレクションで装備するとプレビューが解除され、装備が保存されること。
- 英語、日本語、赤い服、ImGuiパネル、クールタイム中・完了後の画面をPNG出力し、目視確認。
- 右ドラッグの開始・上下左右の移動・デバッグ画面上までの継続・ボタン解放・キャプチャ喪失での終了。パネル開閉の両方でテスト。
- ImGuiのコマンドからLuckと通常 / 砂漠を切り替え、各景観・Singularityの浮遊ポーズを描画。
- 元データを含まない、独自の4頂点VRMテストカードでWARP上のGLB・スキン・初期モーフ・埋め込みPNG・GPU描画・再読み込み・ファイル不在時のブロック表示を検証。
- ローカルの添付VRM 0.xモデルで124,172頂点・22プリミティブの読み込みとテクスチャ表示を確認。標準Tポーズ、初期向き、ブロックへの切り替え・再選択を画面確認。モデルとその画像は公開リポジトリに含めていません。
- 添付MP3を11,061,696バイトのPCMにデコードし、XAudio2でSingularity表示時のループ開始・他オーラへの変更による停止・再選択での再生を検証。オーラ・景観・音声の設定で履歴が増えないことも確認。

## ローカル測定

| 条件 | 値 |
| --- | --- |
| EXE | 約1.3MiB（ImGui・パーサー・VC++ランタイムを静的リンク） |
| NVIDIA GeForce RTX 3050 6GB Laptop GPU / ブロック | 平均フレーム送信時間 約1.37ms |
| 同GPU / 添付VRM + Singularity + 砂漠 | 約2.02ms、3D描画は合計23 draw calls |
| Microsoft Basic Render Driver / WARP / ブロック | 約2.29ms |
| WARP / 独自4頂点VRMテストカード | 約2.79ms |
| ブロックの3D部分 | 1 draw call（オーラや景観でインスタンス数が変動） |

1440×900、VSyncなし、デバッグパネルを閉じた状態で60フレームを表示せず送信した値です。独立したGPUタイマーの値や実プレイのFPS保証ではありません。通常起動はVSyncを有効にし、ImGuiパネル表示時にはUIの描画バッチが追加されます。

## 再現

```bat
ctest --test-dir build-release --output-on-failure
build-release\StarRng.exe --smoke-test --seed=42 --save-dir=C:\Temp\star-test-save --capture=C:\Temp\star-preview.png --report=C:\Temp\star-report.txt
build-release\StarRng.exe --smoke-test --warp --seed=42 --save-dir=C:\Temp\star-warp-save --capture=C:\Temp\star-warp.png --report=C:\Temp\star-warp-report.txt
```

出力先はテスト用の書き込み可能なフォルダーへ変更してください。`--smoke-test`では保存先の明示が必要で、通常のセーブに触れません。キャプチャ指定時は`*-small.png`、`*-cooldown.png`、`*-japanese.png`、`*-debug.png`、`*-complete.png`も出力します。GUIのコマンド処理と描画はスモークテストで検証し、全てのOS入力デバイス操作を自動化するものではありません。

追加で`*-desert.png`と`*-space.png`を生成します。`--vrm="C:\path\avatar.vrm"`を指定するとVRMの読み込み・切り替え・欠落ファイルのテストと`*-vrm.png`を追加します。`--bgm="C:\path\music.mp3"`を指定すると実際の再生・停止も確認します（音声出力デバイスが必要）。通常起動でもこれらの引数でローカルファイルを指定できます。

VRM 1.0も標準形状の読み込み経路を実装していますが、今回の実モデル検証はVRM 0.xです。MToon、Spring Bone、表情・骨アニメーションは検証対象外・未実装です。

GitHub ActionsはWindowsビルド・WARP描画テスト・コアテスト、Ubuntuのコアテストを実行します。
