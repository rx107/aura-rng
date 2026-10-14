# フレームワークと拡張

## 現在の流れ

Win32入力 → `App` がクールダウンを確認 → `Roller` がチケット抽選 → `Progress` が所持数を更新 → CSVログ追記とセーブ更新 → `Frame` を `Renderer` に渡して描画します。

`aura_core` はWin32画面やDirectXデバイスを参照しません。CSVの読み込み、確率、進行データ、ログを単独でテストできます。Windows上でのセーブ置き換えだけはOS APIを利用します。

`Renderer` は読み取り用の `Frame` を受け取り、コレクションを書き換えません。UIのクリックは `Hit` を返し、変更は `App` が処理します。ゲーム状態を保持したまま描画デバイスを作り直せます。

`App` が起動／終了、入力、手動・自動ロール、保存を管理します。例外をWin32ウィンドウプロシージャーから外に投げないよう、I/Oと描画の更新はゲームループ側で実行します。

## 次に追加できるもの

1. **オーラ作成GUI**: `aura_core` を再利用した別EXE。定義編集、色選択、ライブプレビュー、CSVの検証付き出力。保存前の入力検証をコアと共通化。
2. **演出の種類**: 現在のリング／粒子に加えて、`effect_type` と `schema_version` を導入。独自メッシュや複数エミッターを導入する際は描画予算を設ける。
3. **バイオーム**: 時間・天候を管理する状態と適用条件を追加。通常確率とブレイクスルー確率を別々のデータとして扱う。
4. **Luck / 装備 / ポーション**: 一時的なプレイヤー効果をまとめた抽選コンテキストを追加。確率の合計が1を超える場合の規則を先に決める。
5. **クラフト**: `Progress` の所持数を消費するレシピ、重複オーラの使い道、確認画面。
6. **オンライン化**: サーバー側抽選、アカウント、同期。クライアントのローカル乱数／セーブを信用する設計のままランキングを導入しない。

初版はシングルプレイヤーの試作です。物理エンジン・衝突判定は含めず、アバターはステージ内に制限しています。木や柱は背景物です。音、モデル取り込み、ゲーム内課金もまだありません。

## 描画に関する参照

Windows標準のDirect3D 11デバイスをBGRA対応で作成し、DXGIサーフェス上にDirect2D UIを描きます。

- [Microsoft: Direct2D / Direct3D interoperability](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-direct3d-interoperation-overview)
- [Microsoft: Direct3D 11 instanced rendering](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexedinstanced)
