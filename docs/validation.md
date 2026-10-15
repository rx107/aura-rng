# Star's RNG Alpha 0.1 検証記録

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

## Windows描画・入力テスト

NVIDIA GPUとWARPの両方で次を確認しています。

- Direct3D 11、Direct2D、Dear ImGui Win32 / DX11バックエンドの初期化・描画・終了。
- ボタンクリック、Space、F、Wの入力経路と1000×700 / 1440×900のリサイズ。
- 全12オーラの描画。
- 通常・自動ロールでもコレクションの装備を維持。
- デバッグの未所持プレビューがCSVのファイルサイズも含めて履歴を変更せず、自動ロールを停止。
- コレクションで装備するとプレビューが解除され、装備が保存されること。
- 英語、日本語、赤い服、ImGuiパネル、クールタイム中・完了後の画面をPNG出力し、目視確認。

## ローカル測定

| 条件 | 値 |
| --- | --- |
| EXE | 1,047,040バイト（約1.00MiB、ImGui・VC++ランタイムを静的リンク） |
| NVIDIA GeForce RTX 3050 6GB Laptop GPU | 平均フレーム送信時間 約1.01ms |
| Microsoft Basic Render Driver / WARP | 平均フレーム送信時間 約1.63ms |
| 3D部分 | 1 draw call、Verdant装備時355インスタンス |

1440×900、VSyncなし、デバッグパネルを閉じた状態で60フレームを表示せず送信した値です。独立したGPUタイマーの値や実プレイのFPS保証ではありません。通常起動はVSyncを有効にし、ImGuiパネル表示時にはUIの描画バッチが追加されます。

## 再現

```bat
ctest --test-dir build-release --output-on-failure
build-release\StarRng.exe --smoke-test --seed=42 --save-dir=C:\Temp\star-test-save --capture=C:\Temp\star-preview.png --report=C:\Temp\star-report.txt
build-release\StarRng.exe --smoke-test --warp --seed=42 --save-dir=C:\Temp\star-warp-save --capture=C:\Temp\star-warp.png --report=C:\Temp\star-warp-report.txt
```

出力先はテスト用の書き込み可能なフォルダーへ変更してください。`--smoke-test`では保存先の明示が必要で、通常のセーブに触れません。キャプチャ指定時は`*-small.png`、`*-cooldown.png`、`*-japanese.png`、`*-debug.png`、`*-complete.png`も出力します。GUIのコマンド処理と描画はスモークテストで検証し、全てのOS入力デバイス操作を自動化するものではありません。

GitHub ActionsはWindowsビルド・WARP描画テスト・コアテスト、Ubuntuのコアテストを実行します。
