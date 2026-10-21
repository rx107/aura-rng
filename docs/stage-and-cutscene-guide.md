# 惑星の移動と、カットシーンの調整

![惑星を選択する画面](planet-select-preview.webp)

## ロケットで別の惑星へ行く

1. ゲーム画面の右上、ワールド枠にある **「惑星を選択 / 移動」** を押します。
2. 行き先のカードを押すとロケットが出発します。
3. 離陸 → 星の海を移動 → 着陸の後、新しいステージを歩けます。

庭園・砂漠・海・森・火山・Extraを選べます。Extraは丸みのある黄色い星の上のステージです。現在いる惑星には「現在地」と表示します。
**Mキー** でも選択画面を開けます。右上の **×** または **M / Esc** で閉じられます。
移動では、所持オーラ・装備オーラ・ロール回数を変更しません。

**F1 → ワールド → 惑星 / ステージ** からは、ロケット演出を挟まずに景観を直接切り替えられます。
ステージの制作中に、すぐ見比べたいときに使ってください。

ロケットは各惑星のリスポーン地点にあります。Extraの限定オーラと、Collectionの4分野については[IllusionaryとExtraのガイド](illusionary-and-extra-guide.md)を参照してください。

## カットシーンを試す

**F1 → オーラ / Auras → Equinox または BreakThrough → カットシーンのみ再生**で確認できます。試しに再生しても、オーラの所持数とRoll Historyは増えません。AutoRollは停止します。

Equinoxは最後の0.8秒で映像と音楽を一緒にフェードし、対応するオーラへ切り替えます。BreakThroughは終わる直前まで映像と音量を保ち、30秒で即座に対応オーラへ切り替えます。入手時もF1の確認用再生も同じ動きです。

終了後の表示は一時プレビューなので、Collectionで選んだ装備は変更しません。F1の装備表示へ戻す操作、またはCollectionでの装備選択で通常の表示へ戻せます。再生中に**Esc**で中断した場合は、再生前の表示へ戻ります。

カットシーンの映像はコードで描いています。参考MP4を動画として画面へ貼り付けているわけではありません。音声だけを切り出したファイルは同梱しているため、元の動画がなくても音付きで確認できます。参考画像と元動画は配布しません。

| カットシーン | 同梱する音声 | 演出の長さ |
| --- | --- | --- |
| Equinox | `data/audio/Equinox_Cutscene.m4a` | 10.9秒 |
| BreakThrough | `data/audio/Breakthrough_Cutscene.m4a` | 30秒 |

同梱音声がない場合は、手元のDownloadsの`EquinoxReworkedCutscene.mp4.mp4`または`BreakthroughCutsceneV2.mp4.mp4`から音声を探します。BreakThroughの元動画を使う場合だけ、4.5秒を開始位置として読み飛ばします。同梱音声はその区間を切り出し済みなので、0秒から使います。音声が見つからない場合も映像は再生できますが、Equinoxの低音に合わせた揺れは発生しません。

## BreakThroughの場面とカメラ

v0.1.14では、i・iiと後半を初実装のv0.1.6へ戻し、iii・ivはv0.1.13の映像を維持しました。柱・構造物は静止し、カメラの動きで奥行きを出します。場面の境目は映像を重ねずに切り替え、元の絞り閉鎖・開口も保ちます。冒頭0.25秒の導入フェードと、iiiの冒頭で画面が開く演出も残します。

| 場面 | 時間 | カメラの動き |
| --- | --- | --- |
| i — started with few | 0〜3.4秒 | 初実装の近接カメラ。柱の上部を斜めに映し、少しずつ回り込む |
| ii — only less it gets. | 3.4〜7.3秒 | 初実装の近接カメラ。柱の低い部分へ切り替わり、斜めの画面がゆっくり回る |
| iii — time by time, now empty | 7.3〜11.1秒 | 中央の光のコアへ一直線に接近。後半ほど加速する |
| iv — and there was nothing left. | 11.1〜14.15秒 | ホワイトアウトから視界が戻り、巨大な直方体が複数ある空間へ。広角で光源へゆっくり直進し、カメラだけが少し上下する |
| get out of my head | 14.15〜15.05秒 | 完全な黒画面へ切り替え、14.2〜15秒だけ文字を表示する |
| it wasn't gone〜最後 | 15.05〜30秒 | 初実装の7本柱の全景に戻り、少しずつ後退しながら回り込む。最後は元の絞り閉鎖と短い閃光を経て、通常のオーラ表示へ即切り替える |

ivの文章は13秒から表示します。`with only 5 fragments left`は、`it wasn't gone`と同じ傾きと横移動で表示します。最後の28〜29秒の絞り閉鎖と29.05秒の短い閃光は、初実装からの映像内の演出です。終了時に通常画面へ重ねるフェードは行いません。

この動きの初期値は `src/tuning/cutscene.hpp` にあります。以下の値はソース側の設定なので、保存後に `Develop.cmd` で再ビルドしてください。字幕のF1調整と違い、実行中のゲームには保存だけでは反映されません。

| 値 | 初期値 | 数字を変えたとき |
| --- | --- | --- |
| `BreakthroughOriginalI` の `distance / targetHeight` | `4.15f / 6.f` | iで柱から離れる距離と、見る場所の高さ。高さを上げると柱の上部を映す |
| 同じ値の `yawSpeed / rollSpeed` | `.035f / .055f` | iで柱の周りを回る速さと、画面が傾く速さ。0でそれぞれ止める |
| `BreakthroughOriginalII` の `distance / targetHeight` | `4.6f / 1.5f` | iiで柱から離れる距離と、見る場所の高さ。距離を下げると柱へ近づく |
| 同じ値の `yawSpeed / rollSpeed` | `.06f / .065f` | iiの周回と画面の傾きの速さ。大きいほど速く動く |
| `BreakthroughCoreRush` の `distanceFrom / distanceTo` | `15.f / .72f` | iiiの最初と最後の距離。最後を小さくするとコアへ深く近づく |
| `BreakthroughRushAcceleration` | `1.65f` | 大きいほどiiiの後半へ加速を寄せる。1なら等速で近づく |
| `BreakthroughCoreFlareDistance` | `9.f` | コアの光が拡大し始める距離。大きいほど早く光に包まれる |
| `BreakthroughWhiteoutSeconds` | `.43f` | ivの閃光から視界が戻る時間。大きいほど白い画面が長く残る |
| `BreakthroughMonolithEnd` | `14.15f` | ivの巨大な柱から黒画面へ切り替える時刻。単位は開始からの秒 |
| `BreakthroughMonolithTravelSeconds` | `18.9f` | ivの前進量を何秒かけて進む速さにするか。小さいほど短いivの中で速く進む |
| `BreakthroughMonolithStartDistance` | `20.f` | ivの開始位置。大きいほど手前から奥を見る |
| `BreakthroughMonolithTravelDistance` | `3.8f` | ivの前進量。大きいほど光源へ近づく |
| `BreakthroughMonolithFocalLength` | `.83f` | 小さいほど広角になり、手前と奥の距離感が強くなる |
| `BreakthroughMonolithBobHeight` | `.055f` | ivのカメラの上下幅。0なら上下動なし |
| `BreakthroughMonolithScale` | `1.f` | ivの巨大な柱全体の倍率。大きいほど圧迫感が増す |
| `BreakthroughBlackoutEnd` | `15.05f` | 黒画面から後半の7本柱へ切り替える時刻 |
| `BreakthroughFinalDistance` / `BreakthroughFinalPullbackSpeed` | `12.2f / .045f` | 後半の最初の距離と、1秒ごとに後退する距離 |
| `BreakthroughFinalYawSpeed` | `.017f` | 後半の7本柱を横へ回り込む速さ。0で回り込みを止める |
| `BreakthroughFloatingCaptionAngle` / `BreakthroughFloatingCaptionSpeed` | `-2.f / 6.f` | `it wasn't gone`と`with only 5 fragments left`の共通の傾きと横移動速度。速度は基準画面高480pxでのpx/秒 |
| `BreakthroughFloatingCaptionMoveSeconds` | `4.f` | 上の2つの文字を何秒間動かすか。出現後この秒数で横移動が止まる |

`BreakthroughOriginalShot`と`BreakthroughShot`の各項目名・数値の並び順は、設定ファイルの冒頭にあります。距離はゲーム内単位、カメラ角度はラジアン、`start / end`はカットシーン開始からの秒です。テロップの`BreakthroughFloatingCaptionAngle`だけは度数です。最初は距離1個だけを変え、F1から再生して比較してください。

通常表示で頭上の名前だけを上げたい場合は、別ファイルの`src/tuning/breakthrough.hpp`にある`titleHeight`を使います。初期値は`3.85f`で、`4.1f`にすると0.25だけ上がります。この値はCutSceneのカメラには影響しません。

## Equinoxの低音に合わせた揺れ

Equinoxのカットシーンは、再生している音声の低音に合わせて、画面全体を軽く上下させます。曲を固定BPMで刻む方式ではなく、読み込んだ音声の35〜180Hz付近の強さを10ミリ秒ごとに調べ、その再生位置の値を使います。

揺れの大きさは `src/tuning/cutscene.hpp` の `EquinoxBassShakeHeight`です。初期値`.008f`は画面高の0.8%で、`.004f`なら半分、`0.f`なら揺れなしになります。低音の計算自体は `src/core/audio_envelope.hpp` にあり、音声読み込み時に一度行います。

## Equinoxのフェードを長く／短くする

`src/tuning/cutscene.hpp` の `EndingFadeSeconds`が、Equinoxの映像と音の切り替え時間です。`.8f`を`1.2f`にすると、終わりの1.2秒でゆっくりオーラへ移ります。カットシーン全体の長さより短い、正の秒数にしてください。変更後は再ビルドが必要です。

BreakThroughは`endingOpacity()`の第3引数`instantSwitch`を`true`にしているため、この値を変更しても最後の即切り替えは変わりません。映像内の導入フェード、場面ごとの絞り閉鎖・開口、短い閃光は保ちます。これらは、終端で通常画面へ重ねるフェードとは別々です。

## サックスに合わせて字幕を少しずつ調整する

**F1 → オーラ → BreakThrough → 字幕タイミング / 秒** を開きます。
英語表示の場合は **Auras → Caption timing / seconds** です。

最初は「全単語の時差」だけ変更するのがおすすめです。

| やりたいこと | 設定の例 | 画面で起きること |
| --- | --- | --- |
| 字幕全体を0.10秒早くする | `-0.10 s` | 全単語の出現が早くなる |
| 字幕全体を0.20秒遅くする | `+0.20 s` | 全単語の出現が遅くなる |
| `believe` だけ直す | `believe` の秒数を調整 | その単語の出現時刻だけ変わる |
| 最初の状態に戻す | 「字幕タイミングを初期値へ」 | 時差が0になり、各単語も初期時刻に戻る |

「カットシーンのみ再生」を押して、音と字幕を確認します。
再生中も **F1** で調整パネルを開閉できます。パネルにはカットシーン開始からの現在時刻が表示されます。
字幕の値を変えたら、もう一度最初から再生して確認すると合わせやすくなります。

`how` から `world?` までの8個の数字は、**カットシーン開始から何秒後に表示するか** を表します。
その数字に「全単語の時差」を足した時刻に表示されます。
たとえば `how = 22.00 s`、全単語の時差 `-0.10 s` なら、`how` は **21.90秒** で表示されます。
順番が入れ替わらないように、各単語は前後の単語の範囲内で調整できます。

音楽や柱の演出の時計は、この設定では動かしません。
変更内容はプレイヤー設定として自動保存されます。**ゲームの再ビルドは不要** です。
通常版と `Develop.cmd` の開発版はセーブ先が異なるため、調整値も別々に保存されます。

## コード側で初期値を変えるとき

初期の字幕時刻は **`src/tuning/cutscene.hpp`** にまとめてあります。
8個の値は `how / much / do / you / believe / in / this / world?` の順番です。
コードを保存して `Develop.cmd` でビルドし、F1の「字幕タイミングを初期値へ」を押して確認してください。
すでに設定ファイルがある場合は保存済みの値が優先されるため、このリセットが必要です。

画面の見た目は `src/render/ui.cpp`、クリックから出発までの処理は `src/platform/main.cpp`、
移動中の時計は `src/core/stages.hpp` が担当します。
`Renderer::stageSelectUi()` が惑星カード、`Renderer::stageTravelUi()` がロケットの絵を描きます。
