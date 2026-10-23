"""専用サーバーを隔離起動し、実WinHTTPクライアント試験EXEを実行します。"""
import argparse
import json
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--scratch-parent", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    parent = args.scratch_parent.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    scratch = parent / ("multiplayer-client-test-" + secrets.token_hex(8))
    scratch.mkdir(mode=0o755)
    source = Path(__file__).resolve().parents[1] / "server" / "serve.py"
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    endpoint = f"http://127.0.0.1:{port}"
    process = None
    try:
        process = subprocess.Popen([sys.executable, str(source), "--host", "127.0.0.1", "--port", str(port),
            "--data-dir", str(scratch / "server")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 5
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        while True:
            if process.poll() is not None:
                raise RuntimeError("Isolated server stopped before client test")
            try:
                with opener.open(endpoint + "/health", timeout=0.3) as response:
                    if json.load(response).get("protocol") == 1:
                        break
            except (urllib.error.URLError, OSError):
                pass
            if time.monotonic() > deadline:
                raise RuntimeError("Isolated server did not become ready")
            time.sleep(0.05)
        result = subprocess.run([str(executable), endpoint, str(scratch / "clients")], timeout=45, check=False)
        return result.returncode
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        # 実行前に作った隔離フォルダーだけを削除します。
        if scratch.resolve().parent != parent or not scratch.name.startswith("multiplayer-client-test-"):
            raise RuntimeError("Unexpected test cleanup path")
        shutil.rmtree(scratch)


if __name__ == "__main__":
    raise SystemExit(main())
