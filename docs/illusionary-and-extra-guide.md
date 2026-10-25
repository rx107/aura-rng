# IllusionaryとExtraの確認・改造ガイド

今回の見た目を試すなら、**F1 → オーラ / Auras → Illusionaryを表示**から始めます。未所持でも確認でき、プレビューで所持数やRoll Historyは増えません。AutoRollは停止します。実際に入手したオーラは、Collectionから装備できます。

## Illusionaryで変わったところ

- アバターは胸を大きく後ろへ反らし、頭を倒して真上を見ます。片腕は肩の高さへ上げ、肘を直角に曲げて前腕を手前へ出します。もう片腕は斜め下・手前へ伸ばします。ブロック・VRMの両方が同じ姿勢を使い、移動中も歩行ポーズを混ぜません。腕が手前へ迫る見え方は、カメラの向きでも変わります。
- `ILLUSIONARY`、`NO REALITY`、`REVERIE`などの文字は、発光のにじみを外して輪郭を残しています。文字の後ろの黒い長方形も取り除きました。黒い軌道の帯や上部の市松模様は演出の一部として残ります。
- 足元の青白チェックは、円周の壁に加えて上面もあります。カメラを上から向けても、中央からステージの地面が透けません。
- 中央の白い光に加え、体の周りを白い光が2つ回ります。2つは反対側から始まり、それぞれ短い白い軌跡を残します。
- 表示中は `data/audio/The_Weirdos.ogg` をBGMとして再生します。F1の音楽設定で別の場所のファイルも指定できます。

## 数字を変えて見比べる

ソース側の数値は、**保存 → ゲームを閉じる → `Develop.cmd` → F1で対象を表示**の順で反映します。一度に一つだけ変えると、何が変化したか分かりやすくなります。

| 開くファイル | 変更する値の例 | 画面で起きること |
| --- | --- | --- |
| `src/tuning/avatar.hpp` の `illusionary` | `TorsoPitch = .55f` を `.35f` | 胸を後ろへ反らす量が弱くなる。脚の姿勢はそのまま。角度はラジアン |
| 同じ場所 | `HeadPitch = .7607963f` を `.60f` | 首だけの反りが弱くなり、顔が少し前へ戻る |
| 同じ場所 | `ElbowPitch`の1番目を `1.5707963f` から `1.20f` | 直角だった肘の曲がりが浅くなる。2番目の`0.f`は伸ばした側 |
| 同じ場所 | `Lift = .90f` を `1.10f` | 姿勢全体が0.20だけ上へ移動する |
| 同じ場所 | `Knee`の2番目を `-.45f` から `-.25f` | 曲げた側の膝が少し伸びる |
| `src/tuning/illusionary.hpp` | `TextSelfLight = 1.22f` を `1.f` | 文字だけが少し暗くなる。周囲の発光は変わらない |
| 同じ場所 | `TitlePixelSize = .043f` を `.050f` | ILLUSIONARYの文字が大きくなる |
| 同じ場所 | `FloorCellSize = .51f` を `.40f` | 床のチェックが細かくなる |
| 同じ場所 | `OrbitLightRadius = 1.92f` を `2.2f` | 白い光が体から遠い場所を回る |
| 同じ場所 | `OrbitLightSpeed = 1.05f` を `.7f` | 白い光の周回がゆっくりになる |
| 同じ場所 | `OrbitTrailAngle = .76f` を `1.f` | 白い光の後ろに続く軌跡が長くなる |

処理そのものは `src/render/illusionary.cpp` を上から読むと追えます。黒い帯 → 放射線 → チェックの壁と床 → 白い周回光と星 → 小さな画面と文字の順です。姿勢の計算は `src/core/animation.cpp` の `illusionaryPose()` が担当します。

`BodyPitch`は全身、`TorsoPitch`は腰より上、`HeadPitch`は首から先を傾けます。初期値は`.26 + .55 + .7607963`で約1.5708ラジアン、つまり90度です。この重ね合わせで天を仰ぎます。`BodyRoll`は横倒しの角度で、初期値は`0.f`です。各腕の配列はアバター自身のXマイナス側・Xプラス側の順なので、画面上の左右と必ず同じにはなりません。

文字や床を描く不透明な板と、ぼかして光らせる板は分けています。文字の明るさだけを変えたいときは `White` の色全体ではなく、`TextSelfLight` を使ってください。

## Extraへ行く

**Mキー**または**惑星を選択 / 移動**から、`Extra`を選びます。丸みのある黄色い星の上がステージで、ロケットの発着場は他の惑星と同じリスポーン地点です。星の輪郭の外側へは歩いて出られません。F1のワールド設定から直接切り替えることもできます。

| Collectionの分野 | 入っているオーラ | ロールで入手できる場所 |
| --- | --- | --- |
| Normal | CommonからSingularityまでの初期オーラ | すべての惑星 |
| Desert | Dune、Mirage、Pharaoh | 砂漠のみ |
| Extra | Pixelation、Equinox、BreakThrough、Illusionary、Frutiger Aero、Red john | Extraのみ |
| Administrator | Nanally | F1の表示確認専用。ロールでは出ない |

Collectionはこの4分野をフォルダーのように切り替えて見られます。表示の分類を切り替えても、現在の惑星や装備は変わりません。以前に入手したオーラは、入手場所から別の惑星へ移動しても装備できます。

Extra限定6種類の**Luck 1での基礎確率**は次のとおりです。

| オーラ | 基礎確率 |
| --- | --- |
| Pixelation | 1 in 2,000,000 |
| Equinox | 1 in 2,500,000 |
| BreakThrough | 1 in 3,000,000 |
| Illusionary | 1 in 4,000,000 |
| Frutiger Aero | 1 in 5,000,000 |
| Red john | 1 in 6,000,000 |

全体ではNormal13種・Desert3種・Extra6種・Administrator1種の計23種で、Nanally以外の22種を入手できます。追加オーラの形を変える手順は[Frutiger Aero](frutiger-aero.md)・[Red john](red-john-guide.md)のガイドへ。F1のプレビューは未所持でも表示でき、マルチプレイ中は相手にも現在の見た目が共有されます。実際の所持数や装備は変わりません。

ExtraでもNormalのオーラを入手できます。Dune・Mirage・Pharaohは砂漠限定です。惑星で抽選対象が変わり、Stardustはその惑星の抽選表で残った確率を受け持ちます。Luckを変えると実際の抽選分布も変わります。

確率は `data/auras.csv`、分類と出現条件は `src/core/aura.cpp` の `auraCategory()` / `availableInBiome()` にあります。オーラのIDはセーブと対応する名前なので、既存のIDを気軽に変えないでください。

## Extraの星を改造する

`src/tuning/stages.hpp` の `Extra`で始まる値が入口です。

| 値 | 初期値 | 変更の効果 |
| --- | --- | --- |
| `ExtraInnerRadiusRatio` | `.55f` | 大きくすると星の谷が浅くなり、ふっくらする |
| `ExtraOuterRadiusRatio` | `.97f` | 大きくすると星の先端が遠くへ伸びる |
| `ExtraPlanetThickness` | `2.2f` | 大きくすると横から見た星の床が厚くなる |
| `ExtraSkyStarCount` | `54` | 増やすと周囲に浮かぶ小さな星が増える |

輪郭と歩行範囲は `src/core/stages.hpp` の同じ計算を使います。見た目だけ大きくして歩行範囲を置き去りにすることを防いでいます。全体の広さを変えたい場合は、`src/tuning/world.hpp` の `HalfExtent` も確認してください。

カットシーンの試し方と時間の調整は、[惑星とカットシーンのガイド](stage-and-cutscene-guide.md)へ。将来の機能に必要な準備は[今後の設計メモ](next-features.md)にまとめています。

![Extraの星形ステージ全体](planet-extra-preview.webp)

![CollectionのExtraフォルダー](collection-folders-preview.webp)

![上から見たチェック床](illusionary-floor-preview.webp)
