#!/usr/bin/env python3
"""Red john の短いオリジナル導入音を、Python の標準ライブラリだけで作る。

音楽の切り抜き・音声サンプル・外部サービスは使いません。短い鍵盤とベース、
指鳴らし風の乾いた音を数式から合成します。参考動画の音声とは別の自作曲です。

再生成: python tools/generate_redjohn_audio.py
保存先を変更: python tools/generate_redjohn_audio.py --output my_intro.wav
出来た WAV を差し替えるだけなら C++ の再ビルドは不要です。
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import math
from pathlib import Path
import random
import sys
import wave

# ── まず触る設定 ──
SAMPLE_RATE = 22_050  # 1 秒間の音の点の数。軽量な mono / 16-bit PCM で保存する。
DURATION = 11.0      # 最後の余韻まで含めた秒数。画面の導入は約 10.9 秒。
MASTER_PEAK = 0.82   # 0〜1 の最大音量。1 未満にして音割れを避ける。
END_FADE = 0.9       # 終わりを静かにする秒数。大きくすると早く消え始める。
KEY_VOLUME = 0.12    # 鍵盤の強さ。大きくするとメロディーが前へ出る。
BASS_VOLUME = 0.19   # 短いベースの強さ。
SNAP_VOLUME = 0.19   # 帯の切替に合わせた乾いたアクセントの強さ。
SLIDE_VOLUME = 0.018 # 帯が滑るときの控えめな空気音。0 なら消える。

# 帯、目、横顔、題名、題名の着地、最後の切替と同じ「再生開始からの秒」。
# tuning/redjohn.hpp の開始時刻を変えたときは、こちらも同じ秒へ合わせて再生成する。
PANEL_ACCENTS = (2.05, 2.85, 3.80, 5.45, 6.05, 8.75)

# MIDI は音の高さの番号。60=中央のド、+12 で 1 オクターブ上。
# 時刻・音番号・長さ・強さを並べたオリジナルの短いフレーズです。
KEY_NOTES = (
    (0.28, 74, .64, .72), (0.80, 69, .58, .65),
    (1.30, 73, .60, .70), (1.72, 76, .46, .62),
    (2.05, 78, .76, .85), (2.44, 76, .54, .60),
    (2.85, 73, .74, .77), (3.31, 69, .55, .58),
    (3.80, 71, .80, .78), (4.31, 74, .68, .62),
    (4.84, 78, .57, .64), (5.45, 76, .64, .80),
    (6.05, 73, .82, .77), (6.66, 69, .60, .58),
    (7.28, 71, .68, .62), (7.87, 74, .68, .65),
    (8.75, 73, 1.14, .75), (9.10, 78, 1.20, .50),
)
BASS_NOTES = (
    (0.28, 38), (1.30, 45), (2.05, 47), (2.85, 42),
    (3.80, 43), (4.84, 50), (5.45, 45), (6.05, 40),
    (7.28, 43), (8.75, 38),
)


def frequency(midi: float) -> float:
    """鍵盤の番号を、1 秒間の振動数 Hz へ変換する。"""
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def render() -> array:
    """各楽器を足し合わせ、端のフェードと音割れ対策をして PCM を返す。"""
    count = round(SAMPLE_RATE * DURATION)
    mix = [0.0] * count
    rng = random.Random(20261025)  # 毎回まったく同じ音を生成し、変更点を比較できる。

    def pluck(start: float, midi: int, length: float, volume: float, bass: bool = False) -> None:
        first = round(start * SAMPLE_RATE)
        hz = frequency(midi)
        decay = .27 if bass else .21
        for n in range(round(length * SAMPLE_RATE)):
            index = first + n
            if index >= count:
                break
            t = n / SAMPLE_RATE
            attack = 1.0 - math.exp(-t * (300 if bass else 600))
            envelope = attack * math.exp(-t / decay)
            # 基本の正弦波に、小さく早く消える倍音を足すと弦・鍵盤らしいアタックになる。
            fundamental = math.sin(2 * math.pi * hz * t)
            overtone = .31 * math.sin(2 * math.pi * hz * 2.001 * t) * math.exp(-t * 10)
            overtone += .12 * math.sin(2 * math.pi * hz * 3.003 * t) * math.exp(-t * 21)
            mix[index] += volume * envelope * (fundamental + overtone)

    def snap(start: float, volume: float) -> None:
        # 二つの雑音の差で低音を減らす。短く乾いた「パチッ」で、銃声や爆発は使わない。
        first = round(start * SAMPLE_RATE)
        previous = 0.0
        for n in range(round(.068 * SAMPLE_RATE)):
            index = first + n
            if index >= count:
                break
            t = n / SAMPLE_RATE
            noise = rng.uniform(-1.0, 1.0)
            bright = noise - previous * .72
            previous = noise
            envelope = (1.0 - math.exp(-t * 3500)) * math.exp(-t * 102)
            click = math.sin(2 * math.pi * 1750 * t) * math.exp(-t * 220)
            mix[index] += volume * envelope * (bright * .62 + click * .23)

    def slide(start: float) -> None:
        # 画面の帯が動く前後 0.22 秒だけ、低い音量の空気音を入れる。
        first = round(start * SAMPLE_RATE)
        previous = 0.0
        length = .22
        for n in range(round(length * SAMPLE_RATE)):
            index = first + n
            if index < 0 or index >= count:
                continue
            t = n / SAMPLE_RATE
            u = t / length
            noise = rng.uniform(-1.0, 1.0)
            previous += (noise - previous) * .18
            envelope = math.sin(math.pi * u) ** 2
            mix[index] += SLIDE_VOLUME * envelope * previous

    for at, note, length, strength in KEY_NOTES:
        pluck(at, note, length, KEY_VOLUME * strength)
    for at, note in BASS_NOTES:
        pluck(at, note, .95, BASS_VOLUME, bass=True)

    # 広がりは控えめに。単音の合間へ低い和音を足して、静かなテレビ番組の導入にする。
    for at, notes in ((2.05, (62, 66, 69)), (3.80, (59, 62, 66)),
                      (6.05, (57, 61, 64)), (8.75, (62, 66, 69))):
        for number, note in enumerate(notes):
            pluck(at + number * .025, note, 1.15, KEY_VOLUME * .22)

    for number, at in enumerate(PANEL_ACCENTS):
        snap(at, SNAP_VOLUME * (1 if number in (0, 3, 5) else .80))
        slide(at - .11)

    # オフセット（直流成分）を取り除き、全体の最も大きい点を MASTER_PEAK にそろえる。
    average = sum(mix) / count
    mix = [sample - average for sample in mix]
    peak = max(abs(sample) for sample in mix) or 1.0
    gain = MASTER_PEAK / peak
    pcm = array("h")
    for index, sample in enumerate(mix):
        t = index / SAMPLE_RATE
        fade_in = min(1.0, t / .02)
        fade_out = min(1.0, max(0.0, (DURATION - t) / END_FADE))
        value = sample * gain * fade_in * fade_out
        pcm.append(round(max(-1.0, min(1.0, value)) * 32767))
    if sys.byteorder != "little":
        pcm.byteswap()  # WAV の 16-bit PCM は必ずリトルエンディアン。
    return pcm


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "data/audio/Red_John_Intro.wav")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pcm = render()
    with wave.open(str(args.output), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())
    print(f"Created {args.output.name}: {DURATION:.2f}s, mono {SAMPLE_RATE}Hz PCM16")
    print(f"Bytes: {args.output.stat().st_size}")
    print(f"SHA256: {hashlib.sha256(args.output.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
