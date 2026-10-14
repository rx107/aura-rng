# 初版の検証記録

検証日: 2026-10-15（日本時間）。Windows / x64、Visual C++ 19.39、Windows SDK 10.0.22621.0、CMake Release構成。

## 確認済み

- C++20 / DirectX 11 のゲームEXEと、独立したコアテストEXEをビルド。
- CTest成功。20万回の抽選でシードの再現性と通常オーラの分布を確認。
- 全12オーラのチケット区間の先頭・末尾を検証。10億分の1のオーラもチケット区間が存在することを検証。大量抽選での偶然の当選をテスト条件にはしない。
- 不正な確率合計、重複ID、複数の通常オーラ、NaN色、上限外分母、不正CSVを拒否。
- セーブの上書き、復元、並べ替え、削除したオーラの再追加、破損ファイル検出。
- 日本語・カンマ・引用符を含むオーラ名のCSVログ出力。
- 実際のWin32入力処理を通したROLLボタン、Spaceによるクールダウン、Fによる自動ロールの切り替え、Wによる移動。
- WM_SIZEを通した1000×700 / 1440×900へのリサイズと、両サイズのスクリーンショット確認。
- 全12種類の演出を描画し、Direct3DとDirect2Dの描画完了を検証。
- NVIDIA GPUとWARPソフトウェア描画で、各20ロールとセーブ再読込を検証。

## 測定値

| 条件 | 結果 |
| --- | --- |
| ゲームEXE | 436,224バイト（約426KiB、静的VC++ランタイム） |
| NVIDIA GeForce RTX 3050 6GB Laptop GPU | フレーム送信時間平均 約1.90ms |
| Microsoft Basic Render Driver / WARP | フレーム送信時間平均 約3.29ms |
| 3D描画 | 1 draw call / Verdant装備時355インスタンス |
| 通常起動のCPU側ワーキングセット | 約72.5MiB（1回のスナップショット） |

描画時間は1440×900、VSyncなし、60フレームを表示せず送信したローカル検証の値です。UI描画を含みますが、独立したGPUタイマーによる測定ではなく、実プレイのFPS保証ではありません。GPU専用メモリ量はこのワーキングセットに含みません。環境、ウィンドウサイズ、ドライバーによって変わります。通常プレイはVSyncを有効にします。

## 再現方法

```bat
ctest --test-dir build-release --output-on-failure
build-release\AuraRng.exe --smoke-test --seed=42 --save-dir=C:\Temp\aura-test-save --capture=C:\Temp\aura-preview.png --report=C:\Temp\aura-report.txt
build-release\AuraRng.exe --smoke-test --warp --seed=42 --save-dir=C:\Temp\aura-warp-save --report=C:\Temp\aura-warp-report.txt
```

保存先と出力先は利用できるテスト専用ディレクトリーに置き換えてください。`--smoke-test` では保存先を明示する必要があり、通常のプレイヤーセーブには触れません。キャプチャ指定時は縮小画面の `*-small.png` も出力します。`--seed` はデバッグ用で、通常起動はOS側の乱数ソースから初期化します。

GitHub ActionsにはWindowsでのビルド・コアテスト・WARP描画テストと、Ubuntuでのコアテストを登録しています。オンラインCIの結果はActions画面で確認できます。
