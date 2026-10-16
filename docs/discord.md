# Discord連携とプロフィール

Star's RNGはWindowsデスクトップ版DiscordのRich Presenceへ、ゲーム内ユーザー名と総ロール回数を渡します。ゲーム用の公開Application ID `1560569853184573590`を初期設定に組み込んでいるため、通常はIDを手入力する必要はありません。

## 使い方

1. 同じPCでDiscordデスクトップ版を起動し、ログインします。
2. ゲームで**F1 → Profile / プロフィール**を開きます。
3. **ユーザー名 → ユーザー名を適用**で任意の名前を設定します。既定は`Player`、1〜64 UTF-8バイトです。日本語を使用でき、改行・制御文字は使えません。Discordアカウントのユーザー名を変更する操作ではありません。
4. **Discordにプレイ状況を表示**を有効にします。初期状態では有効です。Application IDは組み込みの値を使用し、別のDiscordアプリを試す場合だけ変更して適用します。
5. ご本人のDiscord設定の**Activity Privacy / アクティビティのプライバシー**で共有を許可します。公開範囲はDiscord側の設定に従います。[Discord公式の共有設定の案内](https://support.discord.com/hc/en-us/articles/7931156448919-Activity-Sharing-on-Discord-FAQ)

送信する表示内容は次の形式です。アプリ名はApplication IDに登録されたStar's RNGです。

```text
Username: Player
Rolls: 12345
```

接続後は名前・回数の変更を5秒以上の間隔で送信します。Discordを後から起動した場合や接続が切れた場合も自動で再接続します。チェックを外すかゲームを終了すると、現在のアクティビティを消去する要求を送ります。Discordの過去のアクティビティ記録を削除する操作ではありません。

## 接続状態の見方

F1のプロフィールタブに状態を表示します。

| 状態 | 意味 |
| --- | --- |
| `Disabled` | ゲーム側のDiscord表示が無効 |
| `Waiting for Discord desktop...` | Discordデスクトップ版を待機中。起動後に自動で再試行 |
| `Connecting to Discord...` | ローカルIPCへの接続・ハンドシェイク中 |
| `Connected / activity updated` | Discordからアクティビティ更新の応答を受信 |
| `Application ID required` / `Invalid Application ID` | IDが空欄または形式が不正。通常は組み込みのIDを使用 |
| `Discord rejected the activity` / `Discord response timed out` | Discordが更新を拒否、または応答がない状態。表示されたエラーとIDを確認 |

`Connected`はIPCの更新応答を受信した意味で、他の利用者から見えることの確認ではありません。実際のDiscordプロフィール上の表示確認は未実施です。表示されない場合は、同じPCのデスクトップ版を使用していること、ゲームのチェックとDiscordの共有設定、Application IDを確認してください。

## 総ロール回数の変更

同じプロフィールタブの**総ロール回数**へ0〜1,000,000,000,000,000を入力し、**ロール回数を適用**を押します。AutoRollを停止して保存し、Discordへ渡す回数も更新します。在庫・最良オーラ・既存の画面履歴・CSVは変わらず、この操作自体もロール履歴に追加しません。その後は新しい回数から通常どおり1ずつ増えます。

過去の`rolls.csv`の番号は書き換えません。回数を戻した場合、今後追記するCSVの番号も新しい回数を起点にするため、過去の番号と重複することがあります。連番を通したまま残したい場合は、回数を戻さずに使用してください。

進行セーブはschema 2で編集可能な`rolls`と在庫合計の`collected`を分離し、在庫の整合性を検証します。旧schema 1は読み込み互換です。プロフィール設定は`settings.txt`のschema 4に保存し、旧schema 1〜3も引き継ぎます。

## 実装と参照

ゲームはWindowsのローカル名前付きパイプを使い、DiscordへRPCのハンドシェイクと`SET_ACTIVITY`を送ります。接続・送受信は専用ワーカーで行い、描画ループでパイプの応答を待ちません。Discord未起動でもゲームを続けられます。ユーザートークン・Botトークン・Client Secretは不要で、追加のSDK DLLも同梱しません。公開Application IDは接続先のアプリを識別する値です。

プロトコルの形式、IPCの接続、ハンドシェイク、`SET_ACTIVITY`は[Discord公式RPCドキュメント](https://docs.discord.com/developers/topics/rpc)を参照してください。ゲーム側の状態とDiscordの実表示は区別し、テストの実施状況は[検証記録](validation.md)に記載します。
