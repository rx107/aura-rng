"""python tests/multiplayer_server_tests.py -- localhost上で専用サーバーを実際に試験。"""
import base64
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import stat
import struct
import tempfile
import threading
import unittest
import zlib

SOURCE = Path(__file__).resolve().parents[1] / "server" / "serve.py"
SPEC = importlib.util.spec_from_file_location("star_rng_server", SOURCE)
server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server)


def png(size=160, ancillary=False):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    rows = (b"\x00" + b"\x22\x88\xee\xff" * size) * size
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
        (chunk(b"tEXt", b"Author\x00private metadata") if ancillary else b"") +
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class MultiplayerServerTests(unittest.TestCase):
    def setUp(self):
        # Windows上の制限付き実行でも開ける、既定ACLを継承した試験用フォルダー。
        self.test_parent = Path.cwd().resolve()
        self.temporary = self.test_parent / (".test-multiplayer-" + secrets.token_hex(8))
        self.temporary.mkdir(mode=0o755)
        self.now = 100.0
        self.world = server.World(self.temporary, clock=lambda: self.now)
        self.http = server.Server(("127.0.0.1", 0), self.world)
        self.thread = threading.Thread(target=self.http.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.http.shutdown()
        self.http.server_close()
        self.thread.join(2)
        self.assertEqual(self.temporary.resolve().parent, self.test_parent)
        self.assertTrue(self.temporary.name.startswith(".test-multiplayer-"))
        shutil.rmtree(self.temporary)

    def request(self, method, path, body=None, token=None, raw=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.http.server_port, timeout=2)
        fields = {"Content-Type": "application/json"}
        if token:
            fields["Authorization"] = "Bearer " + token
        fields.update(headers or {})
        payload = raw if raw is not None else json.dumps(body, separators=(",", ":")).encode() if body is not None else None
        try:
            connection.request(method, path, payload, fields)
            response = connection.getresponse()
            data = response.read()
            return response.status, json.loads(data) if response.getheader("Content-Type") == "application/json" else data
        finally:
            connection.close()

    def join(self, number=1, **changes):
        body = {"protocol": 1, "user_id": str(1_000_000_000 + number), "secret": f"{number:064x}",
            "username": "星 " + str(number), "avatar_png": ""}
        body.update(changes)
        return self.request("POST", "/v1/join", body)

    @staticmethod
    def player(**changes):
        value = {"x": 1.25, "z": -4.5, "yaw": 0.5, "walking": True, "planet": 0,
            "aura_id": "equinox", "aura_level": 2, "shirt": [0.2, 0.6, 0.8]}
        value.update(changes)
        return {"player": value}

    def tick(self, token, **changes):
        self.now += 0.1
        return self.request("POST", "/v1/update", self.player(**changes), token)

    def test_two_clients_movement_aura_and_planets(self):
        status, first = self.join(1)
        self.assertEqual(status, 200)
        status, second = self.join(2)
        self.assertEqual(status, 200)
        self.assertEqual(self.tick(first["token"])[1]["players"], [])
        status, snapshot = self.tick(second["token"], planet=5, x=72, aura_id="frutiger_aero")
        self.assertEqual(status, 200)
        self.assertEqual(snapshot["players"][0]["player"]["x"], 1.25)
        snapshot = self.tick(first["token"], x=9.5, walking=False)[1]
        remote = snapshot["players"][0]
        self.assertEqual(remote["user_id"], second["user_id"])
        self.assertEqual(remote["player"]["planet"], 5)  # 描画側がplanetで絞り込む契約。
        self.assertEqual(remote["player"]["aura_id"], "frutiger_aero")
        self.assertEqual(remote["username"], "星 2")
        self.assertNotIn("secret", json.dumps(snapshot))
        self.assertNotIn("token", json.dumps(snapshot))
        self.assertNotIn("avatar_png", json.dumps(snapshot))

    def test_identity_authentication_restart_and_collision(self):
        first = self.join()[1]
        self.assertEqual(self.join()[0:1], (409,))
        status, conflict = self.join(secret="ff" * 32)
        self.assertEqual((status, conflict["error"]), (409, "identity_conflict"))
        self.assertEqual(self.request("POST", "/v1/leave", {}, first["token"])[0], 200)
        restarted = server.World(self.temporary, clock=lambda: self.now)
        self.assertEqual(restarted.join({"protocol": 1, "user_id": first["user_id"], "secret": f"{1:064x}",
            "username": "Return", "avatar_png": ""})["user_id"], first["user_id"])
        saved = (self.temporary / "identities.json").read_text()
        self.assertNotIn(f"{1:064x}", saved)
        if os.name != "nt":
            self.assertEqual(stat.S_IMODE((self.temporary / "identities.json").stat().st_mode), 0o600)

    def test_disconnect_timeout_and_invalid_sessions(self):
        first, second = self.join(1)[1], self.join(2)[1]
        self.tick(first["token"])
        self.tick(second["token"])
        self.assertEqual(self.request("POST", "/v1/leave", {}, first["token"])[0], 200)
        self.assertEqual(self.tick(first["token"])[0], 401)
        self.assertEqual(self.tick(second["token"])[1]["players"], [])
        self.now += 12.1
        self.assertEqual(self.request("GET", "/health")[1]["players"], 0)
        self.assertEqual(self.tick(second["token"])[0], 401)
        self.assertEqual(self.join(2)[0], 200)
        self.assertEqual(self.request("POST", "/v1/update", self.player())[0], 401)

    def test_profile_photo_roundtrip_and_metadata_removal(self):
        original = png(ancillary=True)
        first = self.join(1, avatar_png=base64.b64encode(original).decode())[1]
        second = self.join(2)[1]
        self.tick(first["token"])
        remote = self.tick(second["token"])[1]["players"][0]
        digest = remote["avatar_hash"]
        path = "/v1/photo/" + first["user_id"] + "/" + digest
        status, photo = self.request("GET", path, token=second["token"])
        self.assertEqual(status, 200)
        self.assertNotIn(b"private metadata", photo)
        self.assertEqual(hashlib.sha256(photo).hexdigest(), digest)
        self.assertEqual(self.request("GET", path)[0], 401)
        self.assertEqual(self.request("GET", path + "?token=" + second["token"])[0], 404)
        self.request("POST", "/v1/leave", {}, first["token"])
        self.assertEqual(self.request("GET", path, token=second["token"])[0], 404)

    def test_invalid_payloads_and_photo_limits(self):
        token = self.join()[1]["token"]
        for changes in ({"x": float("nan")}, {"x": 10 ** 500}, {"z": 1001}, {"walking": 1}, {"planet": 6},
                {"planet": True}, {"aura_level": 4}, {"aura_id": "../secret"}, {"shirt": [0, 2, 0]}):
            self.assertEqual(self.tick(token, **changes)[0], 400, changes)
        self.assertEqual(self.request("POST", "/v1/update", {"player": {}, "secret": "x"}, token)[0], 400)
        self.assertEqual(self.request("POST", "/v1/join", raw=b'{"protocol":1,"protocol":1}')[0], 400)
        self.assertEqual(self.request("POST", "/v1/join", raw=b'\xff')[0], 400)
        self.assertEqual(self.request("POST", "/v1/join", raw=b'[' * 2000)[0], 400)
        self.assertEqual(self.request("POST", "/v1/join", raw=b' ' * (server.MAX_BODY + 1))[0], 413)
        self.assertEqual(self.join(2, username="\n")[0], 400)
        self.assertEqual(self.join(2, username="\ud800")[0], 400)
        self.assertEqual(self.join(2, avatar_png="A" * (server.MAX_PHOTO * 2))[0], 413)
        for bad in (b"not png", png(159), png()[:-1], png() + b"trailing"):
            self.assertEqual(self.join(2, avatar_png=base64.b64encode(bad).decode())[0], 400)
        self.assertEqual(self.request("GET", "/../../identities.json")[0], 404)

    def test_capacity_and_update_rate(self):
        tokens = [self.join(number)[1]["token"] for number in range(1, 33)]
        self.assertEqual(self.join(33), (503, {"error": "server_full"}))
        self.assertEqual(self.tick(tokens[0])[0], 200)
        self.assertEqual(self.request("POST", "/v1/update", self.player(), tokens[0])[0], 429)
        self.assertEqual(self.request("GET", "/health")[1]["players"], 32)
        self.request("POST", "/v1/leave", {}, tokens[0])
        self.assertEqual(self.join(33)[0], 200)


if __name__ == "__main__":
    unittest.main(verbosity=2)
