# フレームワークと拡張

## 現在の流れ

Win32入力 → `App` がクールダウンを確認 → `Roller` がチケット抽選 → `Progress` が所持数を更新 → CSVログ追記とセーブ更新 → `Frame` を `Renderer` に渡して描画します。

`aura_core` はWin32画面やDirectXデバイスを参照しません。CSVの読み込み、確率、進行データ、ログを単独でテストできます。Windows上でのセーブ置き換えだけはOS APIを利用します。

`Renderer` は読み取り用の `Frame` を受け取り、コレクションを書き換えません。UIのクリックは `Hit` を返し、変更は `App` が処理します。ゲーム状態を保持したまま描画デバイスを作り直せます。

`Progress::record()`は所持数と統計だけを更新します。`equipped`はコレクション操作から変更します。`Presentation`は装備と一時プレビューを分離し、プレビュー選択では自動ロールを停止するだけで抽選・ログ処理を呼びません。

ImGuiパネルは`DebugActions`を返し、ゲームループ側で適用します。`Settings` schema 2にLuck・バイオーム・アバター・音量・UTF-8のファイルパスも保存し、schema 1を引き継ぎます。Win32入力はImGuiのキャプチャを確認してからゲームへ渡します。ステージ上から始めた右ドラッグはカメラが所有し、ImGui上へ移動しても継続し、ボタン解放・フォーカス喪失・キャプチャ喪失で終了します。前フレームの`WantCaptureMouse`によるドラッグ中断を避けます。

`Roller::setLuck()`は基礎確率のCDFをL乗し、L回の最良結果と同じ分布を1回の抽選で扱います。高倍率でも合計100%を保ちます。バイオームは現在NormalとDesertの描画状態です。

`VrmAvatar`はcgltfとnlohmann/jsonで自己完結したVRM GLBを読み込み、初期モーフ・スキン・ノード階層をCPUで一度適用します。WICで埋め込みPNG/JPEGを最大2048pxへ変換し、GPUに頂点・インデックス・テクスチャを作成します。読み込みは`std::async`の別スレッドで、D3Dデバイスのリソース作成のみを利用し、即時コンテキストはメインスレッド専用です。読み込み後に巨大な元ファイルとモーフ群を解放します。VRM 0.xと1.0の前方軸の違いを変換します。標準ポーズの表示が対象で、Spring Bone・表情アニメーション・MToonは今後の拡張です。

`Music`はMedia Foundationで音声をPCMへデコードし、XAudio2でループします。`Presentation::shown()`のIDがSingularityのときだけ再生し、それ以外ではフェード後に停止します。音楽は装備表示に追従するため、通常の抽選結果では変わりません。初期値は曲未選択です。

星はベジェ曲線で角を丸めた共有Direct2Dパスです。クールタイムは進行率を左右の位置に対応させ、星と進行済みの線が正弦波の軌跡をたどります。開始・終端で透明度を変え、残り時間0では描画しません。

`App` が起動／終了、入力、手動・自動ロール、保存を管理します。例外をWin32ウィンドウプロシージャーから外に投げないよう、I/Oと描画の更新はゲームループ側で実行します。

## 次に追加できるもの

1. **オーラ作成GUI**: `aura_core` を再利用した別EXE。定義編集、色選択、ライブプレビュー、CSVの検証付き出力。保存前の入力検証をコアと共通化。
2. **演出の種類**: 現在のリング／粒子に加えて、`effect_type` と `schema_version` を導入。独自メッシュや複数エミッターを導入する際は描画予算を設ける。
3. **バイオーム**: 既存の景観状態に時間・天候と適用条件を追加。通常確率とブレイクスルー確率を別々のデータとして扱う。
4. **装備 / ポーション**: 現在のLuck補正に一時効果と有効期限を追加。
5. **クラフト**: `Progress` の所持数を消費するレシピ、重複オーラの使い道、確認画面。
6. **オンライン化**: サーバー側抽選、アカウント、同期。クライアントのローカル乱数／セーブを信用する設計のままランキングを導入しない。

現在はシングルプレイヤーの試作です。物理エンジン・衝突判定は含めず、アバターはステージ内に制限しています。木・サボテン・柱は背景物です。ゲーム内課金はありません。

## 描画に関する参照

Windows標準のDirect3D 11デバイスをBGRA対応で作成し、DXGIサーフェス上にDirect2D UIを描きます。

- [Microsoft: Direct2D / Direct3D interoperability](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-direct3d-interoperation-overview)
- [Microsoft: Direct3D 11 instanced rendering](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexedinstanced)
- [VRM公式: 座標系の変換](https://vrm.dev/api/coordinate/)
