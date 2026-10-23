#!/usr/bin/env python3
"""Star's RNG: 見た目と位置を共有する小さな専用サーバー (Python 3.10以上)。

ゲームの進行を管理するサーバーではありません。公開時はCaddyの後ろで動かします。
パスワード・IDの秘密鍵・セッショントークンをログへ書き出さないでください。
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import zlib

PROTOCOL = 1
MAX_PLAYERS = 32                 # 1台に同時接続できる人数。まずは32人まで。
SESSION_TIMEOUT = 12.0          # この秒数、更新がなければ退出扱い。
MAX_PHOTO = 128 * 1024
MAX_BODY = 192 * 1024
MAX_IDENTITIES = 100_000         # 無制限の登録によるディスク枯渇を防ぐ上限。
ID_RE = re.compile(r"[1-9][0-9]{9}\Z")
SECRET_RE = re.compile(r"[0-9a-f]{64}\Z")
AURA_RE = re.compile(r"[a-z][a-z0-9_]{0,47}\Z")
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")


class ApiError(Exception):
    def __init__(self, status: int, code: str):
        self.status, self.code = status, code


def exact_keys(value, required):
    if not isinstance(value, dict) or set(value) != set(required):
        raise ApiError(400, "invalid_fields")


def text(value, maximum=96):
    try:
        valid_size = isinstance(value, str) and len(value.encode("utf-8")) <= maximum
    except UnicodeError:
        valid_size = False
    if (not valid_size or not value.strip() or
            any(ord(c) < 32 or ord(c) == 127 for c in value)):
        raise ApiError(400, "invalid_text")
    return value.strip()


def number(value, minimum, maximum):
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            value < minimum or value > maximum or not math.isfinite(value)):
        raise ApiError(400, "invalid_number")
    return float(value)


def player_state(value):
    exact_keys(value, ("x", "z", "yaw", "walking", "planet", "aura_id", "aura_level", "shirt"))
    if (type(value["walking"]) is not bool or type(value["planet"]) is not int or
            not 0 <= value["planet"] < 6 or type(value["aura_level"]) is not int or
            not 0 <= value["aura_level"] <= 3 or not isinstance(value["aura_id"], str) or
            not AURA_RE.fullmatch(value["aura_id"]) or not isinstance(value["shirt"], list) or
            len(value["shirt"]) != 3):
        raise ApiError(400, "invalid_player")
    return {"x": number(value["x"], -1000, 1000),
            "z": number(value["z"], -1000, 1000),
            "yaw": number(value["yaw"], -1000, 1000),
            "walking": value["walking"], "planet": value["planet"],
            "aura_id": value["aura_id"], "aura_level": value["aura_level"],
            "shirt": [number(v, 0, 1) for v in value["shirt"]]}


def canonical_png(encoded):
    """160px PNGの構造・CRC・展開サイズを確認し、撮影機器などのメタデータを除去。"""
    if not isinstance(encoded, str) or len(encoded) > (MAX_PHOTO + 2) // 3 * 4:
        raise ApiError(413, "photo_too_large")
    if not encoded:
        return b""
    try:
        data = base64.b64decode(encoded, validate=True)
    except (ValueError, base64.binascii.Error):
        raise ApiError(400, "invalid_photo") from None
    if len(data) > MAX_PHOTO:
        raise ApiError(413, "photo_too_large")
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ApiError(400, "invalid_photo")
    offset, ihdr, compressed, ended = 8, None, bytearray(), False
    while offset + 12 <= len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        if size > MAX_PHOTO or offset + size + 12 > len(data):
            raise ApiError(400, "invalid_photo")
        chunk = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + chunk) & 0xffffffff != crc:
            raise ApiError(400, "invalid_photo")
        if kind == b"IHDR":
            if ihdr is not None or offset != 8 or size != 13:
                raise ApiError(400, "invalid_photo")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunk)
            if (width, height, depth, compression, filtering, interlace) != (160, 160, 8, 0, 0, 0) or color not in (2, 6):
                raise ApiError(400, "photo_must_be_160px_png")
            ihdr = chunk
        elif kind == b"IDAT":
            if ihdr is None:
                raise ApiError(400, "invalid_photo")
            compressed.extend(chunk)
        elif kind == b"IEND":
            if size or offset + 12 != len(data):
                raise ApiError(400, "invalid_photo")
            ended = True
            break
        elif kind[:1].isupper():
            raise ApiError(400, "unsupported_photo")
        offset += size + 12
    if ihdr is None or not ended or not compressed:
        raise ApiError(400, "invalid_photo")
    channels = 4 if ihdr[9] == 6 else 3
    expected = 160 * (1 + 160 * channels)
    try:
        decoder = zlib.decompressobj()
        pixels = decoder.decompress(bytes(compressed), expected + 1)
        if len(pixels) != expected or not decoder.eof or decoder.unused_data:
            raise ValueError()
        if any(pixels[y * (1 + 160 * channels)] > 4 for y in range(160)):
            raise ValueError()
    except zlib.error:
        raise ApiError(400, "invalid_photo") from None
    except ValueError:
        raise ApiError(400, "invalid_photo") from None

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", bytes(compressed)) + chunk(b"IEND", b"")


class World:
    def __init__(self, directory, *, clock=time.monotonic, timeout=SESSION_TIMEOUT):
        self.directory, self.clock, self.timeout = Path(directory), clock, timeout
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700 if os.name != "nt" else 0o755)
        self.path = self.directory / "identities.json"
        self.lock = threading.RLock()
        self.sessions = {}
        self.join_attempts = {}
        self.identities = {}
        if self.path.exists():
            loaded = json.loads(self.path.read_text("utf-8"))
            if not isinstance(loaded, dict) or loaded.get("schema") != 1 or not isinstance(loaded.get("identities"), dict):
                raise RuntimeError("Invalid identity database; restore the private backup.")
            self.identities = loaded["identities"]
            for user_id, record in self.identities.items():
                if (not ID_RE.fullmatch(user_id) or not isinstance(record, dict) or
                        set(record) != {"salt", "proof"} or not SECRET_RE.fullmatch(record["salt"]) or
                        not SECRET_RE.fullmatch(record["proof"])):
                    raise RuntimeError("Invalid identity database; restore the private backup.")

    def save(self):
        # 同じフォルダー内の一時ファイルから置換。中途半端なJSONを残しません。
        temporary = self.path.with_suffix(".tmp")
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump({"schema": 1, "identities": self.identities}, stream, separators=(",", ":"))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, self.path)
        if os.name != "nt":
            os.chmod(self.path, 0o600)

    @staticmethod
    def proof(salt, secret):
        # 秘密鍵は256bit乱数なので、辞書攻撃を想定した人間のパスワードとは違います。
        return hashlib.sha256(bytes.fromhex(salt) + bytes.fromhex(secret)).hexdigest()

    def cleanup(self):
        now = self.clock()
        for token in [token for token, session in self.sessions.items() if now - session["seen"] > self.timeout]:
            del self.sessions[token]

    def join(self, body):
        exact_keys(body, ("protocol", "user_id", "secret", "username", "avatar_png"))
        if type(body["protocol"]) is not int or body["protocol"] != PROTOCOL:
            raise ApiError(400, "protocol_mismatch")
        user_id, secret = body["user_id"], body["secret"]
        if not isinstance(user_id, str) or not ID_RE.fullmatch(user_id) or not isinstance(secret, str) or not SECRET_RE.fullmatch(secret):
            raise ApiError(400, "invalid_identity")
        username, photo = text(body["username"]), canonical_png(body["avatar_png"])
        with self.lock:
            self.cleanup()
            record = self.identities.get(user_id)
            if record and not hmac.compare_digest(record["proof"], self.proof(record["salt"], secret)):
                raise ApiError(409, "identity_conflict")
            if any(s["user_id"] == user_id for s in self.sessions.values()):
                raise ApiError(409, "already_connected")
            if len(self.sessions) >= MAX_PLAYERS:
                raise ApiError(503, "server_full")
            if record is None:
                if len(self.identities) >= MAX_IDENTITIES:
                    raise ApiError(503, "registration_full")
                salt = secrets.token_hex(32)
                self.identities[user_id] = {"salt": salt, "proof": self.proof(salt, secret)}
                try:
                    self.save()
                except OSError:
                    del self.identities[user_id]
                    raise ApiError(503, "storage_unavailable") from None
            token = secrets.token_hex(32)
            self.sessions[token] = {"user_id": user_id, "username": username, "photo": photo,
                "avatar_hash": hashlib.sha256(photo).hexdigest() if photo else "",
                "seen": self.clock(), "last_update": -1.0, "player": None}
            return {"protocol": PROTOCOL, "token": token, "user_id": user_id, "update_hz": 10, "timeout": self.timeout}

    def allow_join(self, address):
        # 32人が同じ回線から参加する場合も許容しつつ、登録連打を抑えます。
        with self.lock:
            now = self.clock()
            for key in [key for key, record in self.join_attempts.items() if now - record[0] > 60]:
                del self.join_attempts[key]
            if address not in self.join_attempts:
                if len(self.join_attempts) >= 4096:
                    raise ApiError(429, "join_rate_limited")
                self.join_attempts[address] = [now, 0]
            record = self.join_attempts[address]
            if record[1] >= 60:
                raise ApiError(429, "join_rate_limited")
            record[1] += 1

    def session(self, token):
        self.cleanup()
        if not isinstance(token, str) or not SECRET_RE.fullmatch(token) or token not in self.sessions:
            raise ApiError(401, "invalid_session")
        return self.sessions[token]

    def update(self, token, body):
        exact_keys(body, ("player",))
        player = player_state(body["player"])
        with self.lock:
            session = self.session(token)
            now = self.clock()
            if now - session["last_update"] < 0.04:
                raise ApiError(429, "update_too_fast")
            session.update(player=player, seen=now, last_update=now)
            return {"protocol": PROTOCOL, "players": [{"user_id": s["user_id"], "username": s["username"],
                "avatar_hash": s["avatar_hash"], "player": s["player"]}
                for s in self.sessions.values() if s["player"] is not None and s is not session]}

    def leave(self, token):
        with self.lock:
            self.session(token)
            del self.sessions[token]
        return {"ok": True}

    def photo(self, token, user_id, photo_hash):
        with self.lock:
            self.session(token)
            for session in self.sessions.values():
                if session["user_id"] == user_id and session["avatar_hash"] == photo_hash and session["photo"]:
                    return session["photo"]
        raise ApiError(404, "photo_not_found")


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    def __init__(self, address, world):
        self.world, self.slots = world, threading.BoundedSemaphore(64)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version, sys_version = "StarRng/1", ""

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, *_):
        pass  # 既定のアクセスログを止め、秘密鍵やトークンの出力を避けます。

    def reply(self, status, data, content_type="application/json"):
        if content_type == "application/json":
            data = json.dumps(data, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def token(self):
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            raise ApiError(401, "missing_session")
        return header[7:]

    def body(self):
        if self.headers.get("Transfer-Encoding") or self.headers.get_content_type() != "application/json":
            self.close_connection = True
            raise ApiError(400, "json_required")
        raw_size = self.headers.get("Content-Length", "")
        if not raw_size.isdigit() or len(raw_size) > 8:
            self.close_connection = True
            raise ApiError(400, "invalid_length")
        size = int(raw_size)
        if size > MAX_BODY:
            self.close_connection = True
            raise ApiError(413, "body_too_large")
        try:
            raw = self.rfile.read(size)
            if len(raw) != size:
                raise ValueError()
            # NaN/Infinity/重複キーも受け付けません。
            def pairs(items):
                result = {}
                for key, value in items:
                    if key in result:
                        raise ValueError()
                    result[key] = value
                return result
            return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
        except (UnicodeError, ValueError, RecursionError):
            raise ApiError(400, "invalid_json") from None

    def handle_api(self, method):
        try:
            if method == "GET" and self.path == "/health":
                with self.server.world.lock:
                    self.server.world.cleanup()
                    count = len(self.server.world.sessions)
                self.reply(200, {"ok": True, "protocol": PROTOCOL, "players": count, "max_players": MAX_PLAYERS})
            elif method == "GET" and re.fullmatch(r"/v1/photo/[1-9][0-9]{9}/[0-9a-f]{64}", self.path):
                _, _, _, user_id, photo_hash = self.path.split("/")
                self.reply(200, self.server.world.photo(self.token(), user_id, photo_hash), "image/png")
            elif method == "POST" and self.path == "/v1/join":
                body = self.body()
                self.server.world.allow_join(self.client_address[0])
                self.reply(200, self.server.world.join(body))
            elif method == "POST" and self.path == "/v1/update":
                self.reply(200, self.server.world.update(self.token(), self.body()))
            elif method == "POST" and self.path == "/v1/leave":
                exact_keys(self.body(), ())
                self.reply(200, self.server.world.leave(self.token()))
            else:
                self.close_connection = True
                raise ApiError(404, "not_found")
        except ApiError as error:
            try:
                self.reply(error.status, {"error": error.code})
            except OSError:
                self.close_connection = True
        except (ConnectionError, TimeoutError, OSError):
            self.close_connection = True

    def do_GET(self):
        self.handle_api("GET")

    def do_POST(self):
        self.handle_api("POST")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--data-dir", type=Path, default=Path("server-data"))
    arguments = parser.parse_args()
    server = Server((arguments.host, arguments.port), World(arguments.data_dir))
    print(f"Star's RNG server: {arguments.host}:{arguments.port} (protocol {PROTOCOL})", flush=True)
    def stop(*_):
        threading.Thread(target=server.shutdown, daemon=True).start()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
