"""Local HTTP smoke tests: NEVER load the user's config.json or call an AI API."""
import ast
import http.client
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import threading
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class WebServerSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="wih-web-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        # Explicit allowlist: do not copy/read the real config or other root files.
        for name in ("server.py", "index.html", "serial-bridge.js"):
            shutil.copyfile(ROOT / name, folder / name)
        (folder / "config.json").write_text(json.dumps({
            "apiKey": "dummy-test-value-not-a-secret",
            "baseUrl": "https://example.invalid/v1",
            "model": "test-only",
        }), encoding="utf-8")
        spec = importlib.util.spec_from_file_location("wih_test_server", folder / "server.py")
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)
        cls.upstream = patch.object(
            cls.module, "request_upstream",
            side_effect=AssertionError("Tests must never contact an upstream API"))
        cls.upstream_mock = cls.upstream.start()
        cls.addClassCleanup(cls.upstream.stop)
        cls.server = cls.module.ThreadingHTTPServer(("127.0.0.1", 0), cls.module.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.addClassCleanup(cls.stop_server)

    @classmethod
    def stop_server(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)

    def request(self, method, path, body=None):
        connection = http.client.HTTPConnection(
            "127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.request(method, path, body=body,
                               headers={"Content-Type": "application/json"})
            response = connection.getresponse()
            return response.status, response.getheaders(), response.read()
        finally:
            connection.close()

    def tearDown(self):
        self.upstream_mock.assert_not_called()

    def test_server_python_syntax(self):
        ast.parse((ROOT / "server.py").read_text(encoding="utf-8"))

    def test_index_serves_serial_module(self):
        status, headers, data = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"import { WIHSerialBridge }", data)
        self.assertIn(b'id="chip-toggle"', data)
        self.assertIn(("Cache-Control", "no-store"), headers)

    def test_serial_module_served_as_javascript(self):
        status, headers, data = self.request("GET", "/serial-bridge.js")
        self.assertEqual(status, 200)
        content_type = dict(headers)["Content-type"]
        self.assertTrue("javascript" in content_type or "ecmascript" in content_type)
        self.assertIn(b"export class WIHSerialBridge", data)

    def test_direct_sensitive_paths_forbidden(self):
        # Only checks the existing direct-path guards, not a full security audit.
        for path in ("/config.json", "/server.py"):
            with self.subTest(path=path):
                status, _, data = self.request("GET", path)
                self.assertEqual(status, 403)
                self.assertFalse(json.loads(data)["ok"])

    def test_invalid_json_rejected(self):
        status, _, data = self.request("POST", "/api/muse", b"{bad")
        self.assertEqual(status, 400)
        self.assertFalse(json.loads(data)["ok"])

    def test_empty_sentence_and_unknown_mode_rejected(self):
        for payload in ({"sentence": ""}, {"sentence": "hello", "mode": "invalid"}):
            with self.subTest(payload=payload):
                status, _, data = self.request("POST", "/api/muse", json.dumps(payload))
                self.assertEqual(status, 400)
                self.assertFalse(json.loads(data)["ok"])

    def test_unknown_post_route_rejected(self):
        status, _, data = self.request("POST", "/does-not-exist")
        self.assertEqual(status, 404)
        self.assertFalse(json.loads(data)["ok"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
