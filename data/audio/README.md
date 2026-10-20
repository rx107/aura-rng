# オーラ用BGM

| ファイル | 再生条件 | 配布 |
| --- | --- | --- |
| `Tides_of_the_Outer_Reach.mp3` | Singularity表示中 | ソース・公開ZIPに同梱 |
| `Redline_Shift.mp3` | Pixelation Level 1〜2 | ソース・公開ZIPに同梱 |
| `Censored.mp3.mpeg` | Pixelation Level 3 | ソース・公開ZIPに同梱 |
| `The_Weirdos.ogg` | Illusionary表示中 | ソース・公開ZIPに同梱 |
| `Equinox_Cutscene.m4a` | Equinoxのカットシーン | 音声のみ。ソース・公開ZIPに同梱 |
| `Breakthrough_Cutscene.m4a` | BreakThroughのカットシーン | 音声のみ。ソース・公開ZIPに同梱 |

これらはプロジェクト作者から提供されたBGMと参考動画の音声です。提供者の許可により、GitHubのソースと配布パッケージに同梱しています。各楽曲の権利はそれぞれの権利者に帰属し、ゲームのコードのライセンスとは別です。

通常は`data/audio`の同梱ファイルを使い、`Develop.cmd`でビルドした場合も実行先へコピーされます。オーラBGMは同名ファイルをDownloadsへ置く方法、F1 → Audio / 音楽で任意の場所の曲を選ぶ方法にも対応しています。設定で指定したパスを優先し、初期設定では同梱ファイル、Downloadsの順に探します。音量は音楽・効果音で共通です。

Pixelation Level 0とEquinoxの通常表示にはBGMを設定していません。発射・衝突の効果音はコードから合成するオリジナルPCMで、音源ファイルを必要としません。

`The_Weirdos.ogg`は同梱の小さなOgg Vorbisデコーダーで読み込みます。追加のWindowsコーデックをインストールせずに再生できます。MP3・M4A・MP4の音声はWindows Media Foundation、音声出力はXAudio2を使います。読み込みエラーはF1の音楽欄で確認できます。

## カットシーンの音声

参考動画から必要な区間の**音声だけ**を切り出して同梱しています。Equinoxは10.9秒、BreakThroughは元動画の4.5秒から30秒の区間です。同梱ファイルは切り出し済みなので、どちらも0秒から再生します。映像のエフェクトはコードで再現しています。

同梱音声がない場合は、Downloadsにある`EquinoxReworkedCutscene.mp4.mp4` / `BreakthroughCutsceneV2.mp4.mp4`を探します。元動画から読む場合だけ、BreakThroughの開始位置を4.5秒にします。同梱音声がある環境では、元のMP4を置く必要はありません。

最後は映像と同じフェードで音楽を下げながら、対応するオーラ表示へ移ります。F1の確認用再生でも同じです。フェード時間は `src/tuning/cutscene.hpp` の `EndingFadeSeconds`で調整します。

Equinoxの画面の揺れは、実際に読み込んだ音声の35〜180Hz付近の強さを使います。固定BPMで揺らしているわけではありません。音声ファイルがない、音声を読めない、または無音の場合は低音の値が0となり、低音による揺れは発生しません。音声がない環境でも映像演出そのものは確認できます。

VRM・参考画像・GIF・参考MP4は、ソースと公開ZIPのどちらにも同梱しません。
