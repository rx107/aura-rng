# オーラ用BGM

| ファイル | 再生条件 | 配布 |
| --- | --- | --- |
| `Tides_of_the_Outer_Reach.mp3` | Singularity表示中 | ソース・公開ZIPに同梱 |
| `Redline_Shift.mp3` | Pixelation Level 1〜2 | ソース・公開ZIPに同梱 |
| `Censored.mp3.mpeg` | Pixelation Level 3 | ソース・公開ZIPに同梱 |
| `The_Weirdos.ogg` | Illusionary表示中 | ソース・公開ZIPに同梱 |
| `Koraii_Layers_Cut.mpeg` | Equinoxの通常表示中 | ソース・公開ZIPに同梱 |
| `Equinox_Cutscene.m4a` | Equinoxのカットシーン | 音声のみ。ソース・公開ZIPに同梱 |
| `Breakthrough_Cutscene.m4a` | BreakThroughのカットシーン | 音声のみ。ソース・公開ZIPに同梱 |
| `Red_John_Cutscene.m4a` | Red johnのカットシーン | 提供MP4のAACを再圧縮せず抽出した音声のみ。許可を得てソース・公開ZIPに同梱 |

8ファイルはプロジェクト作者から提供されたBGMと参考動画の音声です。提供者の許可により、GitHubのソースと配布パッケージに同梱しています。各楽曲の権利はそれぞれの権利者に帰属し、ゲームのコードのライセンスとは別です。

通常は`data/audio`の同梱ファイルを使い、`Develop.cmd`でビルドした場合も実行先へコピーされます。オーラBGMは同名ファイルをDownloadsへ置く方法、F1 → Audio / 音楽で任意の場所の曲を選ぶ方法にも対応しています。設定で指定したパスを優先し、初期設定では同梱ファイル、Downloadsの順に探します。音量は音楽・効果音で共通です。

Pixelation Level 0にはBGMを設定していません。発射・衝突の効果音はコードから合成するオリジナルPCMで、音源ファイルを必要としません。

`The_Weirdos.ogg`は同梱の小さなOgg Vorbisデコーダーで読み込みます。追加のWindowsコーデックをインストールせずに再生できます。MP3・MPEG・M4A・MP4の音声はWindows Media Foundation、音声出力はXAudio2を使います。読み込みエラーはF1の音楽欄で確認できます。

## Equinoxの通常BGMと拍

`Koraii_Layers_Cut.mpeg`は5,929,920バイト、148.24秒の通常表示用BGMです。Equinoxを表示すると再生し、別のオーラへ変えると停止します。CutSceneではこの曲を使わず、従来の`Equinox_Cutscene.m4a`を使います。

初期値は通常曲が**180 BPM・時差+0.044秒**、CutSceneが**180 BPM・時差+0.056秒**です。通常曲は148秒全体の288箇所の主拍からテンポと先頭の拍を計測しました。

**F1 → 音楽**から通常表示とCutSceneのBPM・拍の時差を別々に変更して保存できます。BPMは30〜300、時差は-2〜+2秒で、正の時差なら拍を遅らせます。BPMは1分間の拍数なので、180なら1拍が約0.333秒です。通常表示の玉の周回・白黒交換・トレイル・爆発・床文字と、CutSceneの軽い上下の揺れは、実際に再生されている音声の時刻から拍を求めます。

標準の通常表示は4拍で玉が1周し、1拍ごとに白黒交換と短い爆発、4拍ごとに「陰→陽→衡→無」と床文字が進みます。音源を差し替えた場合は、BPMと時差もその曲へ合わせてください。C++の再ビルドは不要です。

## カットシーンの音声

参考動画から必要な区間の**音声だけ**を切り出して同梱しています。Equinoxは10.9秒、BreakThroughは元動画の4.5〜34.5秒の30秒間です。同梱ファイルは切り出し済みなので、どちらも0秒から再生します。映像のエフェクトはコードで再現しています。

同梱音声がない場合は、Downloadsにある`EquinoxReworkedCutscene.mp4.mp4` / `BreakthroughCutsceneV2.mp4.mp4`を探します。元動画から読む場合だけ、BreakThroughの開始位置を4.5秒にします。同梱音声がある環境では、元のMP4を置く必要はありません。

Equinoxは最後0.8秒で映像と音楽を同時にフェードし、対応するオーラ表示へ移ります。フェード時間は`src/tuning/cutscene.hpp`の`EndingFadeSeconds`で調整します。BreakThroughは30秒まで映像・音量を保ち、終端で即座にオーラへ切り替えます。F1の確認用再生も同じです。

Equinoxの画面の揺れは、F1で設定したCutSceneのBPMと時差を使って拍へ合わせます。以前の生の低音の大きさをそのまま使う方式ではありません。Pixelationの自動BPM推定用には、音声読み込み時に35〜180Hz付近の低音を計測します。音声がない環境でも映像演出そのものは確認できます。

VRM・参考画像・GIF・参考MP4は、ソースと公開ZIPのどちらにも同梱しません。

## Red john のカットシーン音声

v0.1.19から`Red_John_Cutscene.m4a`は、提供された`The-Mentalist-SnapYT.App.mp4`の音声を抽出したファイルです。元動画の0秒から約10.89秒のAAC音声を再圧縮せずM4Aへ取り出し、元の音質を保っています。提供者の明示的な許可を得てGitHubのソースと配布ZIPに同梱しています。元MP4の映像・ファイルは同梱しません。通常のオーラ表示中は再生せず、入手時とF1の確認用CutSceneで再生します。

演出は従来どおり10.9秒で、0秒から再生し、最後の0.8秒で映像と一緒にフェードします。部屋・ポーズ・映像・確率は変更していません。

`tools/generate_redjohn_audio.py`はv0.1.18で使っていた旧合成曲を試すために残した任意ツールです。現在の音声を再生成するものではなく、通常のビルドでも実行しません。旧曲を試す場合は`python tools/generate_redjohn_audio.py --output old_intro.wav`のように別名へ出力すると、現在の配布音声を上書きせずに聴き比べられます。
