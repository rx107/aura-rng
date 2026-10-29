#!/usr/bin/env python3
"""Create the three public ZIP files after the game and its tests have passed.

例: python tools/package_release.py --windows-folder package --dest dist
     --tag v0.1.21-alpha.1
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
    "red_john_cutscene.m4a",  # 提供MP4のAACを再圧縮せず抽出。提供者の許可を得たCutScene音声
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

## 全面ゲーム画面と左右のメニュー

- ステージ・アバター・オーラをウィンドウの内側全体へ表示します。旧Collection・Roll History・黒い外枠の常設パネルを外し、左上の画像ロゴと下中央のRoll / AutoRollを重ねます。Windowsのウィンドウモードは従来どおりです。
- 左上の星を押すと0.65秒で1回転し、回り終わってから左メニューが開きます。オーラ → マルチプレイヤー → 設定 → クレジットの4項目は、すべてダミーボタンです。押しても画面遷移や設定変更は行いません。
- 右下の三本線から、実際に動作するImGuiデバッグ、マルチプレイヤーIP設定、ゲーム終了を選べます。接続先画面ではURL/IPと名前を入力し、保存・接続・切断できます。保存だけでは自動接続しません。終了は確認なしで、通常の保存処理を通して閉じます。
- 左右メニューは不透明度68%の四角いパネルです。幅・位置・不透明度・星の回転時間はsrc/tuning/shell.hppへまとめ、docs/screen-menu-guide.mdに「値を変えると画面がどう変わるか」を説明しています。
- Mの惑星選択とF1のデバッグは継続して使えます。Escで開いたメニューや接続先画面を閉じ、何も開いていないときは右メニューを開きます。旧Collectionや履歴の常設表示を外しても、所持数・実装備・ログのデータとF1のオーラプレビューは引き継ぎます。

オーラの演出・確率・所持品・8つの音源・3枚のブランドPNG・マルチプレイの通信形式は変更していません。左上のロケットは従来どおり5秒で輪を1周します。星を押したときの回転時間とは別の設定です。

## 検証と配布

公開ジョブはWindowsのビルド・CTest・DirectX WARP描画試験と、Linuxのコア・サーバー試験が成功してから実行します。
画面全体の3D表示、星の1回転後のメニュー、左4項目のダミー動作、右メニューの実操作、接続先の保存、サイズ変更後の表示とクリック判定を確認します。
提供されたブランド画像3枚はUI素材としてソースとWindows ZIPへ同梱します。私有VRM、オーラの参考動画・画像、個人セーブ・プロフィール写真・秘密鍵は同梱しません。
ZIPのCRCと公開ファイルを検査し、SHA256SUMS.txtを添付します。

## 互換性・使い方

既存の専用サーバーと互換性があります（protocol=1）。VPSの更新や契約変更は不要です。
展開した新しいStarRng.exeを起動してください。ゲーム内の保存先は従来どおりです。
改造する場合はsource ZIPを使い、docs/screen-menu-guide.mdを開いてください。初回のビルドはdocs/development-guide.mdで説明しています。
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--windows-folder", type=Path, required=True, help="CMake --installで作ったWindows配布フォルダー")
    parser.add_argument("--dest", type=Path, default=Path("dist"), help="ZIPとSHA256SUMS.txtの保存先（初期値: dist）")
    parser.add_argument("--tag", required=True, help="CMakeのVERSIONと一致するタグ。例: v0.1.21-alpha.1")
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
