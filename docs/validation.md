# Star's RNG Alpha 0.1 / v0.1.7 検証記録

検証日: 2026-10-17（日本時間）。Windows x64、Visual C++ 19.39、Windows SDK 10.0.22621.0、Release / C++20 / MT。

## 自動テスト

- コアテスト成功。20万回のシード再現、Common 1/2・Uncommon 1/4・全13抽選オーラの異なる確率・合計10^12チケットを検証。
- カタログ16項目中、Pixelation・Equinox・BreakThroughの3種類はプレビュー専用。Luck 1〜100万倍でも0チケットで、既存13種類の確率は追加前と同一。
- 旧12〜15項目のカタログを含むセーブ移行、ID並べ替え・削除・再追加、所持数と装備・Singularity Levelの保持、試作オーラの記録拒否・不正セーブ拒否。
- save schema 1の読込、schema 2の独立した総ロール回数と在庫合計。0回へ変更後のロールでもbestを保持し、回数編集で在庫・既存ログを変更しないことを検証。
- settings schema 1〜3をschema 4へ移行。日本語・引用符入りユーザー名、UTF-8検査、範囲外値、制御文字、パス・プロフィール・Discord設定の保存復元。
- Pixelationは周囲の演出時計と弾・爆発・浮遊の動作時計を分離。周囲はLv0=1倍、Lv1〜2=1.6倍、Lv3=3倍、動作はLv2/3とも1.6倍。20周期で両弾の衝突座標と時刻、爆発の経過、浮遊姿勢が一致し、レベル切替でも発射位置を保持することを検証。
- 固定ポーズは移動・歩行位相から独立。通常の歩行、Singularityの吸収軌道、クールタイムの星とフェードも検証。

## Windows描画・入力・音声

最終ソースをNVIDIA GPUとWARPで実行し、双方で成功しました。

- D3D11 / D2D / Dear ImGuiの初期化、1000×700 / 1440×900リサイズ、Roll・クールタイム・AutoRoll・移動・保存復元。
- 右ドラッグの上下・左右回転、デバッグ開閉中の入力、ボタン解放・capture loss。ロールで装備・表示を変更しないこと。
- 全16オーラの表示経路。Pixelation Level 0〜3、加速、衝突の前後と即時爆発、1周期1回の発射/衝突音、表示切替後の描画クリア。
- Equinoxの5層の動く雷雲と稲光を4秒・40枚の連続画像で確認。大理石状の筋と2球を結ぶ線を廃止。各中心光から独立した波状の光跡と星、約0.7825秒ごとの球とタイトルの白黒交換、最内周に収まる床面の衡、少量の吸込霧を確認。黒文字はブルーム後に描き、黒を保持。
- BreakThroughの7本の塔、ガラス模様、暗い破片、巻く光、2光源、霧を確認。名前をブロックアバターの頭上へ移動。各柱が周期3.6〜4.65秒・振幅0.14〜0.18で上下し、ガラス・破片・上端光も柱と一緒に動くことを通常49枚とCutSceneで確認。
- Equinox約10.9秒 / BreakThrough約30秒のカットシーン。単独再生、AutoRoll停止、入力抑止、リサイズ、自然終了、Esc、F1から停止、種類切替、元の表示・レベル・debug開閉の復元。在庫・CSV・履歴は不変。
- カットシーンはEquinox 60枚、BreakThrough 100枚の連続キャプチャと主要段階の静止画を確認。元動画の転載ではなくコードによる近似のため、煙・塔の模様・細部には差があります。
- ローカルBreakThrough動画の音声を4.5秒から30秒へ切り出して非ループ再生（5,760,000 PCM bytes）。終了/再起動/種類切替で停止と先頭復帰を確認。動画は同梱しません。
- Tides: 11,061,696、Redline Shift: 11,550,144、Censored: 41,290,176 PCM bytesをデコード。表示オーラ/レベルとの連動、ループ、切替、停止を確認。
- ローカルChocolat VRM 124,172頂点 / 22 draw callsをGPUで確認。立ち姿、歩行、全固定ポーズの移動時保持、テクスチャ、再読込・欠落時フォールバック。WARPは独自24頂点fixtureを使用。VRMは公開資産へ含めません。
- プロフィールの日本語名とDiscord設定の保存、総回数0 / 123456789 / 元の値の適用を実アプリ処理で確認。AutoRollを止め、在庫・best・既存CSVを維持します。

## Discord IPC

Windowsの独立fake named pipeサーバーで、HANDSHAKE v1 → READY → SET_ACTIVITYとnonce応答、名前/回数、JSON/UTF-8、5秒の更新集約、PING/PONG、無効化/終了時の消去、切断・再接続、ERROR/過大パケット、非ブロック更新と終了を検証し成功しました。ゲームのsmokeでは実Discordへの投稿を無効にしています。

前回（v0.1.6）、実Discordデスクトップのプロセスとpipe0の実在を確認しましたが、制限された検証環境からの接続はWindows ERROR_ACCESS_DENIED (5)となりました。**実Discordの更新応答・プロフィール上の表示成功は未確認です。** 既存Discordのアクセス権は変更していません。通常の起動方法と設定は[Discord連携](discord.md)を参照してください。

## ローカル測定

EXE: 1,618,432バイト。追加のゲームエンジン、Discord SDK DLLは使用しません。

| 1440×900の送信時間 | NVIDIA RTX 3050 Laptop | WARP |
| --- | --- | --- |
| 通常ブロック | 1.60419ms | 3.20467ms |
| Pixelation | 3.64996ms | 5.0466ms |
| Equinox | 4.29816ms | 4.53328ms |
| BreakThrough | 5.07264ms | 4.32589ms |
| Equinox CutScene | 1.52195ms | 1.20551ms |

デバッグを閉じ、表示せずVSyncなしで送信した測定です。通常/オーラは60フレーム、Equinox CutSceneは時刻1.5秒固定の20フレーム。GPUタイマー値や実プレイFPSの保証ではありません。VRMモデルや電源・他プロセスなどで変動します。

Pixelation基礎1213、Equinox1026、BreakThrough1225の演出インスタンスを確認。BreakThroughの暗い塔は共有キューブの同一バッチへ追加します。ブルームは全解像度1枚と半解像度2枚を共有し、1440×900で約7.4MiBを初回に確保して再使用。通常オーラでは実行しません。カットシーンは全画面三角形1枚と字幕です。

## 再現

```bat
build.cmd
build-release\StarRng.exe --smoke-test --seed=42 --save-dir=C:\Temp\star-test-save --capture=C:\Temp\star-preview.png --report=C:\Temp\star-report.txt
build-release\StarRng.exe --smoke-test --warp --seed=42 --save-dir=C:\Temp\star-warp-save --report=C:\Temp\star-warp-report.txt
```

テスト用の書き込み可能な保存先を指定してください。通常のセーブには触れません。`--vrm=` / `--bgm=` / `--pixelation-bgm=` / `--pixelation-final-bgm=`でローカル資産の検証を追加できます。参考MP4音声はDownloadsの各原ファイルを使用し、動画・GIF・VRMは再配布しません。許可済みの新2曲は配布ZIPのみに含めます。

GitHub ActionsはWindowsビルド・core/Discord IPCテスト・WARP smokeと、Ubuntu coreテストを実行します。入力検証はWin32メッセージ経路によるもので、全OS入力デバイスの自動操作を保証するものではありません。VRM 1.0経路は実装し、今回の実モデルはVRM 0.x。MToon / Spring Bone / 表情は未実装です。
