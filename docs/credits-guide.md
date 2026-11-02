# クレジットの表示と音を変える

左上の星を押し、メニューの**クレジット**を選ぶと開きます。v0.1.22ではこのボタンを実際の画面へつなぎました。ほかの左3項目は引き続きダミーです。

不透明度30%の灰色のパネルへ、文字と提供された画像を重ねます。上の「credits」は最初から固定表示です。内容は左から少し滑りながらフェードインし、次の7組の順に現れます。

| 順番 | 内容 | 表示場所 |
| --- | --- | --- |
| 1 | Aura Reviewer | 左上 |
| 2 | 顔の画像＋ぜろくんでんせつ | その下 |
| 3 | ブラックホールの画像＋にょ～ん！ | その下 |
| 4 | Illustrator | 左下の見出し |
| 5 | 緑のキャラクター＋にごり(Lv.1) | 見出しの下 |
| 6 | Special Thanks | 右上 |
| 7 | ファンクラブの画像 | 右見出しの下 |

標準では、最初の項目を0.08秒後に開始し、0.18秒ずつずらします。各項目が見えるまで0.45秒、最後の画像まで約1.61秒です。名前と対応画像は同じ組として動きます。

開くときに短い「ポコッ」が1回鳴ります。×・Esc・パネル外のクリックで閉じられ、もう一度開くと最初から現れます。F1へ切り替える場合もクレジットを閉じます。表示中は移動・視点操作・Rollを止め、AutoRollも停止します。閉じてもAutoRollは自動再開しません。後ろのワールドとBGMは続き、所持数・履歴・実装備は変更しません。文字を読みやすくするため、通常のオーラ名やRollボタンなどは一時的に隠し、左上のロゴを残します。

## 最初に背景を少し濃くする

1. `src` → `tuning` → **credits.hpp**を開きます。
2. `PanelOpacity`を探し、`.30f`を`.50f`へ変更します。

```cpp
// 変更前：30%だけ不透明。ゲーム画面がよく見える。
inline constexpr float PanelOpacity=.30f;

// 変更後：50%だけ不透明。灰色のパネルが濃くなる。
inline constexpr float PanelOpacity=.50f;
```

3. Ctrl+Sで保存し、ゲームを閉じて**Develop.cmd**を実行します。
4. F1パネルを閉じ、星 → クレジットで見比べます。

`0`が透明、`1`が不透明です。小数点・末尾の`f`・`;`は残してください。保存だけでは動いているゲームに反映されず、再ビルドが必要です。最初の環境準備は[改造ガイド](development-guide.md)にあります。

この値はクレジットだけに使います。左右のメニューの透明度は`src/tuning/shell.hpp`の別の`PanelOpacity`です。

## フェードと大きさを変える

以下はすべて[src/tuning/credits.hpp](../src/tuning/credits.hpp)にあります。時間は秒、長さは1440×900を基準にした画面座標です。画面が小さい場合はパネル全体を縮め、内容の縦横比を保ちます。

| 設定 | 標準値 → 試す値 | 画面で変わること |
| --- | --- | --- |
| `PanelOpacity` | `.30f → .50f` | 後ろが見えにくくなり、灰色の背景が濃くなる |
| `PanelGray` | `.38f → .50f` | 透明度を変えず、灰色そのものを明るくする。0は黒、1は白 |
| `PanelMaxWidth` | `1120.f → 1200.f` | 空間に余裕があれば、文字と画像も一緒に大きくなる |
| `HorizontalMargin` | `48.f → 80.f` | 左右の余白を広げ、狭い画面ではパネルを小さくする |
| `FirstRevealSeconds` | `.08f → .25f` | 「credits」だけが見える時間を長くする |
| `RevealStaggerSeconds` | `.18f → .30f` | 次の項目が始まるまでの間隔を長くする |
| `FadeSeconds` | `.45f → .70f` | 各項目がゆっくり見えるようになる |
| `SlidePixels` | `28.f → 0.f` | 左からの移動をなくし、フェードだけにする |
| `OutlineWidth` | `1.8f → 2.5f` | 白い文字の輪郭が太くなる |
| `CloseSize` | `32.f → 40.f` | 右上の×の範囲が大きくなる。クリック判定も一緒に変わる |

全項目が揃う時刻は、`FirstRevealSeconds + 6 × RevealStaggerSeconds + FadeSeconds`です。順番を遅くする場合は、まず`RevealStaggerSeconds`だけを変更すると見比べやすくなります。

## 「ポコッ」という音を変える

音は[src/platform/audio.cpp](../src/platform/audio.cpp)の`synthesizeUiPop()`で短いPCM波形を作り、XAudio2で再生します。外部の曲や追加の音声ファイルは使いません。ゲームの音量設定も掛け合わせるため、音量0では鳴りません。

調整する入口は[src/tuning/credits_audio.hpp](../src/tuning/credits_audio.hpp)です。

| 設定 | 標準値 → 試す値 | 耳で変わること |
| --- | --- | --- |
| `Gain` | `.60f → .40f` | 音が控えめになる。0ならクレジット音だけ無音 |
| `DurationSeconds` | `.095f → .13f` | 音の長さを95ミリ秒から130ミリ秒へ延ばす |
| `StartFrequencyHz` | `700.f → 900.f` | 鳴り始めが高くなり、「ピコッ」に近づく |
| `EndFrequencyHz` | `180.f → 140.f` | 音の終わりが低くなる |
| `PitchFallRate` | `48.f → 64.f` | 高い音から低い音へ早く下がる |
| `DecayRate` | `28.f → 36.f` | 音が早く小さくなり、余韻が短くなる |
| `CooldownMilliseconds` | `90 → 140` | 開閉を連打したとき、次の音まで少し長く待つ |

`AttackSeconds=.0015f`は音の立ち上がりです。急な波形の変化で余計なクリック音が混ざらないようにしています。通常はそのままで大丈夫です。音はクレジットを**開いたときだけ**要求し、描画フレームごとや各画像が出るごとには鳴らしません。

## 画像・名前と処理を探す地図

| ファイル | 担当 |
| --- | --- |
| [src/tuning/credits.hpp](../src/tuning/credits.hpp) | 背景、大きさ、順番の間隔、フェードの秒数 |
| [src/core/credits.hpp](../src/core/credits.hpp) | パネルと×の座標、経過秒から透明度・移動距離を計算 |
| [src/render/credits.cpp](../src/render/credits.cpp) | 名前、見出し、画像の位置と描画。文字の輪郭もここで準備 |
| [src/platform/main.cpp](../src/platform/main.cpp) | 画面を開閉し、経過時間を進め、入力とAutoRollを止める |
| [src/tuning/credits_audio.hpp](../src/tuning/credits_audio.hpp) | 音の高さ・音量・長さ |
| [src/platform/audio.cpp](../src/platform/audio.cpp) | 「ポコッ」のPCM波形を生成し、XAudio2で効果音を再生 |

名前を変える場合は`credits.cpp`の`words`配列を探します。表示順と位置は同じファイルの下側の`creditsUi()`、フェードの計算は`core/credits.hpp`です。文字の輪郭と画像は起動・リサイズ時に準備し、表示フレームごとのファイル読込は行いません。

画像は次の4枚です。差し替えるときは同じファイル名のPNGを保存し、ゲームを閉じて`Develop.cmd`で再ビルドしてください。`build-release/data`のコピーだけを変えると、次のビルドで元へ戻るため、元の`data/ui`を編集します。

| 名前・用途 | 画像 |
| --- | --- |
| ぜろくんでんせつ | `data/ui/credits-reviewer-zero.png` |
| にょ～ん！ | `data/ui/credits-reviewer-nyon.png` |
| にごり(Lv.1) | `data/ui/credits-illustrator.png` |
| Special Thanks | `data/ui/credits-special-thanks.png` |

提供されたWebPは、読み込み形式を揃えるためPNGへ変換しました。変換後も復号したRGBA画素は一致し、透明度・色・縦横比を維持しています。ブラックホールのPNGは元ファイルとバイト単位で一致します。全体配置を示した参考画像は配布へ入れず、この4枚をゲーム用素材として同梱します。

この変更では通信形式はprotocol 1のままで、既存VPSの更新は不要です。
