#!/usr/bin/env python3
"""Create the three public ZIP files after the game and its tests have passed.

例: python tools/package_release.py --windows-folder package --dest dist
     --tag v0.1.18-alpha.1
通常はGitに登録したファイルだけをソースZIPへ入れます。Gitのない作業用
フォルダーでは --paths-file public-paths.json で明示した一覧を使えます。
このスクリプト自体はビルド・通信・GitHubへの公開を行いません。
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]

# 音楽を追加する時は、公開の許可を確認してからこの一覧へ追加します。
# Downloadsフォルダーの音楽を自動収集することはありません。
APPROVED_AUDIO = {
    "tides_of_the_outer_reach.mp3",
    "redline_shift.mp3",
    "censored.mp3.mpeg",
    "the_weirdos.ogg",
    "equinox_cutscene.m4a",
    "breakthrough_cutscene.m4a",
    "koraii_layers_cut.mpeg",
    "red_john_intro.wav",  # tools/generate_redjohn_audio.pyで数式から合成したオリジナル導入音
}
AUDIO_SUFFIXES = {".mp3", ".mpeg", ".ogg", ".m4a", ".wav", ".flac", ".aac", ".opus", ".wma", ".aif", ".aiff"}
PRIVATE_NAMES = {
    "multiplayer_identity.json", "identities.json", "profile.png",
    "progress.txt", "settings.txt", "rolls.csv", "id_rsa", "id_ed25519",
    "id_ecdsa", "authorized_keys", "known_hosts", ".env",
}
PRIVATE_FOLDERS = {".git", ".ssh", "__pycache__", "server-data", "local-save", "multiplayer-test-data"}
FORBIDDEN_SUFFIXES = {
    ".vrm", ".glb", ".gltf", ".mp4", ".gif", ".avi", ".mov", ".mkv",
    ".webm", ".wmv", ".m4v", ".pyc", ".pyo", ".pem", ".key", ".pfx",
    ".p12", ".ppk", ".zip", ".7z", ".rar",
}
REFERENCE_NAMES = {
    "chocolat", "newpixelationcollection", "equinoxnewcollection",
    "nanally-ultimate", "breakthroughnewcollection", "illusionaryingamegif",
    "codex-clipboard-",
    "the-mentalist",
}
TEXT_SUFFIXES = {".py", ".cpp", ".hpp", ".h", ".c", ".md", ".txt", ".json", ".yml", ".yaml", ".cmd", ".service", ".example", ".csv", ".manifest"}
SERVER_FILES = (
    ("Start-local-server.cmd", "Start-local-server.cmd"),
    ("server/serve.py", "server/serve.py"),
    ("server/Caddyfile.example", "server/Caddyfile.example"),
    ("server/stars-rng.service", "server/stars-rng.service"),
    ("README.md", "docs/multiplayer.md"),
    ("multiplayer-preview.webp", "docs/multiplayer-preview.webp"),
)


def version_from_cmake() -> str:
    """CMakeListsのVERSIONが、ZIP名を決める唯一のバージョン番号です。"""
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    match = re.search(r"\bproject\s*\(\s*StarsRng\s+VERSION\s+(\d+\.\d+\.\d+)\b", cmake, re.IGNORECASE)
    if not match:
        raise ValueError("CMakeLists.txt does not declare the StarsRng project version")
    return match.group(1)


def checked_path(value: str) -> str:
    """ZIPの中から外へ出るパスや、Windows固有の絶対パスを拒否します。"""
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or "\0" in value:
        raise ValueError(f"Invalid relative package path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in value.split("/")):
        raise ValueError(f"Invalid relative package path: {value!r}")
    return path.as_posix()


def audit_file(folder: Path, relative: str, *, runtime: bool = False) -> Path:
    """公開禁止ファイルが1つでもあれば、ZIPを作る前に処理を止めます。"""
    relative = checked_path(relative)
    path = folder / relative
    # リンク先にある私物が、意図せず公開物へ混ざるのを防ぎます。
    for part in (path, *path.parents):
        if part == folder:
            break
        if part.is_symlink():
            raise ValueError(f"Symlinks are not allowed in release inputs: {relative}")
    if not path.resolve().is_relative_to(folder.resolve()) or not path.is_file():
        raise ValueError(f"Missing file or path outside package root: {relative}")
    lowered = PurePosixPath(relative.lower())
    name, suffix = lowered.name, lowered.suffix
    if name in PRIVATE_NAMES or any(part in PRIVATE_FOLDERS or part.startswith(".test-multiplayer-") for part in lowered.parts):
        raise ValueError(f"Private/runtime file cannot be published: {relative}")
    if name.startswith(".env.") or suffix in FORBIDDEN_SUFFIXES or any(fragment in name for fragment in REFERENCE_NAMES):
        raise ValueError(f"Private asset, key, archive or reference file cannot be published: {relative}")
    if suffix in AUDIO_SUFFIXES and (name not in APPROVED_AUDIO or lowered.parts[:2] != ("data", "audio") or len(lowered.parts) != 3):
        raise ValueError(f"Audio is not in the approved public music list: {relative}")
    if suffix == ".exe" and (not runtime or relative != "StarRng.exe"):
        raise ValueError(f"Only the installed StarRng.exe belongs in the Windows ZIP: {relative}")

    # 秘密鍵・実運用ホスト・トークンを、拡張子を変えただけで混ぜないための検査。
    # 文書に書く接続先は rng.example.com のような例示用ドメインにします。
    contents = path.read_bytes()
    if re.search(rb"-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----", contents):
        raise ValueError(f"Private key contents detected: {relative}")
    if re.search(rb"\bgh[pousr]_[A-Za-z0-9]{20,}\b", contents):
        raise ValueError(f"Credential contents detected: {relative}")
    if re.search(rb"[a-z0-9-]+\.xvps\.jp", contents, re.IGNORECASE):
        raise ValueError(f"Deployment hostname must not be bundled: {relative}")
    if suffix in TEXT_SUFFIXES or name in {".gitignore", "cmakelists.txt"}:
        for candidate in re.findall(rb"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])", contents):
            try:
                address = ipaddress.ip_address(candidate.decode("ascii"))
            except ValueError:
                continue
            # localhost・LAN・例示用のIPは説明とテストに必要。実在の公開IPは入れません。
            if address.is_global:
                raise ValueError(f"Public deployment IP must not be bundled: {relative}")
    return path


def source_paths(paths_file: Path | None) -> list[str]:
    if paths_file is None:
        # -zで区切ると、スペースや日本語のあるファイル名も壊れません。
        result = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, stdout=subprocess.PIPE)
        values = result.stdout.decode("utf-8").rstrip("\0").split("\0")
    else:
        text = paths_file.read_text(encoding="utf-8-sig")
        values = json.loads(text) if text.lstrip().startswith("[") else text.splitlines()
    if not isinstance(values, list) or not values:
        raise ValueError("The public source path list must be a non-empty list")
    result = [checked_path(value) for value in values]
    if len(result) != len(set(result)) or len(result) != len({path.casefold() for path in result}):
        raise ValueError("The public source path list contains duplicate/case-colliding paths")
    return sorted(result)


def make_zip(destination: Path, top: str, entries: list[tuple[str, Path]]) -> Path:
    """全ZIPに外側のフォルダーを付け、展開時に他のファイルと混ざらないようにします。"""
    archive = destination / f"{top}.zip"
    temporary = destination / f"{top}.zip.tmp"
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
            for relative, source in sorted(entries):
                output.write(source, f"{top}/{checked_path(relative)}")
        with zipfile.ZipFile(temporary) as check:
            bad = check.testzip()
            if bad:
                raise ValueError(f"ZIP CRC check failed: {bad}")
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
    return archive


def release_notes(tag: str) -> str:
    return f"""Star's RNG Alpha 0.1 / {tag}

## 新オーラ Red john

- 淡い壁の部屋、低いベッド、赤い笑顔をコードで描くExtraオーラを追加しました。基礎確率は1 in 6,000,000、Extraステージだけで入手できます。
- アバターは片膝を立て、片腕を膝へ乗せ、反対の手でベッドを支えます。ブロックとVRMで姿勢を共有し、移動中も固定ポーズを保ちます。写真の人物の顔や服装は取り込みません。
- 約10.9秒の専用CutSceneを追加しました。参考動画のテレビ番組風の導入を手がかりに、黒い導入、青・白・黄色の帯、目、アバターの横顔、大きなRED JOHNの文字をGPUとDirect2Dで描きます。参考動画を同梱・再生する方式ではありません。
- F1のオーラ一覧と「カットシーンのみ再生」から未所持でも確認できます。最後まで再生するとRed johnを一時表示し、Esc・停止では元の表示へ戻ります。プレビューは所持数・実装備・Roll Historyを変えず、AutoRollを停止します。
- 家具・顔の大きさ、姿勢、演出の各時刻を自分で変えられる日本語ガイドを追加しました。初期のCommon 1/2・Uncommon 1/4と、既存セーブの所持品・装備を引き継ぎます。

## 音と素材

Red johnの配布音声はtools/generate_redjohn_audio.pyで数式から合成したオリジナルの導入曲です。
参考動画の音声・映像、参考写真、私有VRMは配布しません。許可済みの従来7音源は引き続き同梱しています。

## 検証

公開ジョブはWindowsのビルド・CTest・DirectX WARP描画試験と、Linuxのコア・サーバー試験が成功してから実行します。
コア試験では23種の確率・ステージ制限、旧セーブの読み込み、座りポーズと通常歩行の分離、プレビュー非登録を確認します。
描画試験ではRed johnの部屋、ブロック・VRMの姿勢、視点を回したときの壁、CutSceneの時刻・終了・中止・画面サイズ変更を確認します。
ZIP作成時には私有VRM・参考動画・個人セーブ・写真・秘密鍵の混入とZIPのCRCを検査し、SHA256SUMS.txtを添付します。

## 互換性・使い方

既存の専用サーバーと互換性があります（protocol=1）。サーバーの更新や契約変更は不要です。
ゲームを展開してStarRng.exeを起動し、Multiplayerで普段のHTTPS URLへ接続してください。
ロール回数・所持品は各PCに保存します。自分のVRMや個人セーブは配布ZIPへ含めません。
改造する場合はsource ZIP、専用サーバーを新しく設置する場合だけserver ZIPを使ってください。
Red johnの改造手順はdocs/red-john-guide.md、設定値の一覧はsrc/tuning/README.mdにあります。
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--windows-folder", type=Path, required=True, help="CMake --installで作ったWindows配布フォルダー")
    parser.add_argument("--dest", type=Path, default=Path("dist"), help="ZIPとSHA256SUMS.txtの保存先（初期値: dist）")
    parser.add_argument("--tag", required=True, help="CMakeのVERSIONと一致するタグ。例: v0.1.18-alpha.1")
    parser.add_argument("--paths-file", type=Path, help="Gitがない手元の作業用: 公開パスのJSON配列または1行1パスの一覧")
    args = parser.parse_args()
    version = version_from_cmake()
    if not re.fullmatch(r"v" + re.escape(version) + r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", args.tag):
        raise ValueError(f"Release tag must match CMake VERSION {version}: {args.tag}")
    windows = args.windows_folder.resolve()
    if not windows.is_dir() or windows.is_symlink():
        raise ValueError("--windows-folder must be an installed Windows package directory")
    source = source_paths(args.paths_file)
    if "CMakeLists.txt" not in source or "src/platform/main.cpp" not in source:
        raise ValueError("The source path list does not contain the game project")
    # 先に全入力を検査します。失敗した場合は新しいZIPを作りません。
    source_entries = [(relative, audit_file(ROOT, relative)) for relative in source]
    windows_entries = []
    for path in sorted(windows.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Symlinks are not allowed in Windows input: {path.name}")
        if path.is_file():
            relative = path.relative_to(windows).as_posix()
            windows_entries.append((relative, audit_file(windows, relative, runtime=True)))
    executable = windows / "StarRng.exe"
    if not executable.is_file() or executable.read_bytes()[:2] != b"MZ":
        raise ValueError("The Windows package does not contain a valid StarRng.exe")
    server_entries = [(target, audit_file(ROOT, relative)) for target, relative in SERVER_FILES]
    destination = args.dest.resolve()
    # 配布物自身を次回の配布入力へ再帰的に取り込まないよう、別のフォルダーにします。
    if destination == windows or destination.is_relative_to(windows):
        raise ValueError("--dest must be outside --windows-folder")
    destination.mkdir(parents=True, exist_ok=True)
    archives = [
        make_zip(destination, f"StarRng-Alpha0.1-v{version}-windows-x64", windows_entries),
        make_zip(destination, f"StarRng-v{version}-source", source_entries),
        make_zip(destination, f"StarRng-v{version}-server", server_entries),
    ]
    lines = []
    for archive in archives:
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        lines.append(f"{digest}  {archive.name}\n")
        print(f"Created {archive.name} ({archive.stat().st_size:,} bytes)")
    (destination / "SHA256SUMS.txt").write_text("".join(lines), encoding="utf-8", newline="\n")
    (destination / "release-notes.md").write_text(release_notes(args.tag), encoding="utf-8", newline="\n")
    print("Public file audit / ZIP CRC / SHA-256: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"Release packaging stopped: {error}", file=sys.stderr)
        raise SystemExit(1)
