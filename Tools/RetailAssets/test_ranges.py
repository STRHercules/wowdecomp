"""Exercise the compiled CASC entry point against a local HTTP server; no Blizzard downloads."""
import hashlib
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import unittest

PROBE = sys.argv.pop(1)
PAYLOAD = b"BLTE\0\0\0\0Nrange fixture"
KEY = hashlib.md5(PAYLOAD).hexdigest()
ARCHIVE = "22" * 16


class Handler(http.server.BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *args):
        pass

    def do_GET(self):
        scenario = self.path.split("/")[1]
        self.requests.append((scenario, self.headers.get("Range"), self.path))
        assert self.path.endswith(f"/data/22/22/{ARCHIVE}"), self.path
        assert self.headers["Range"] == f"bytes=123-{122 + len(PAYLOAD)}"
        self.send_response(200 if scenario == "ignored" else 206)
        first = 124 if scenario == "wrong-range" else 123
        self.send_header("Content-Range", f"bytes {first}-{first + len(PAYLOAD) - 1}/268435456")
        self.send_header("Content-Length", str(len(PAYLOAD) + (1 if scenario == "wrong-length" else 0)))
        self.send_header("Connection", "close")
        self.end_headers()
        body = PAYLOAD[:-3] if scenario == "truncated" else PAYLOAD
        if scenario == "bad-hash":
            body = PAYLOAD[:-1] + b"!"
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


class RangeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def run_probe(self, root, scenario, success):
        subprocess.run([PROBE, str(root), f"127.0.0.1:{self.server.server_port}", scenario,
                        "success" if success else "failure"], check=True, timeout=15)

    def test_range_and_warm_reuse(self):
        with tempfile.TemporaryDirectory() as directory:
            before = len(Handler.requests)
            self.run_probe(directory, "range", True)
            self.assertEqual(len(Handler.requests) - before, 1)
            files = [p for p in pathlib.Path(directory).rglob("*") if p.is_file()]
            self.assertEqual(len(files), 1)
            self.assertEqual(files[0].name, KEY)
            self.assertEqual(files[0].read_bytes(), PAYLOAD)

    def test_reject_bad_responses(self):
        for scenario in ("ignored", "wrong-range", "wrong-length", "truncated", "bad-hash"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as directory:
                self.run_probe(directory, scenario, False)
                self.assertEqual([p for p in pathlib.Path(directory).rglob("*") if p.is_file()], [])

    def test_repair_invalid_cached_slice(self):
        with tempfile.TemporaryDirectory() as directory:
            cached = pathlib.Path(directory) / "data" / KEY[:2] / KEY[2:4] / KEY
            cached.parent.mkdir(parents=True)
            cached.write_bytes(PAYLOAD[:-1] + b"!")
            self.run_probe(directory, "range", True)
            self.assertEqual(cached.read_bytes(), PAYLOAD)

    def test_extract_existing_archive_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = pathlib.Path(directory) / "data/22/22" / ARCHIVE
            archive.parent.mkdir(parents=True)
            archive.write_bytes(b"x" * 123 + PAYLOAD + b"unrelated data")
            before = len(Handler.requests)
            self.run_probe(directory, "range", True)
            self.assertEqual(len(Handler.requests), before)
            self.assertTrue(archive.is_file())


if __name__ == "__main__":
    unittest.main()
