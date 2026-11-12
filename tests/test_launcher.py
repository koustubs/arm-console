import http.client
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import launcher


class UploadTests(unittest.TestCase):
    def setUp(self):
        launcher.job.update(state="running", output="")

    def test_compile_precedes_verified_normal_upload(self):
        calls = []
        def fake_cli(*args, **kwargs):
            calls.append(args)
            return "ok"
        with patch.object(launcher, "ports", return_value=[{"address": "COM7"}]), patch.object(launcher, "cli", side_effect=fake_cli):
            launcher.perform("upload", "COM7", "arm")
        self.assertEqual(launcher.job["state"], "success")
        self.assertEqual([call[0] for call in calls], ["compile", "upload"])
        self.assertIn("--verify", calls[1])
        self.assertIn("COM7", calls[1])
        self.assertNotIn("--programmer", calls[1])

    def test_missing_port_never_compiles_or_uploads(self):
        with patch.object(launcher, "ports", return_value=[]), patch.object(launcher, "cli") as fake_cli:
            launcher.perform("upload", "COM7", "arm")
        self.assertEqual(launcher.job["state"], "error")
        fake_cli.assert_not_called()

    def test_compile_failure_never_uploads(self):
        with patch.object(launcher, "ports", return_value=[{"address": "COM7"}]), patch.object(launcher, "cli", side_effect=RuntimeError("compile failed")) as fake_cli:
            launcher.perform("upload", "COM7", "original")
        self.assertEqual(launcher.job["state"], "error")
        self.assertEqual(fake_cli.call_args.args[0], "compile")
        self.assertEqual(fake_cli.call_count, 1)

    def test_port_disappearing_after_compile_prevents_upload(self):
        with patch.object(launcher, "ports", side_effect=[[{"address": "COM7"}], []]), patch.object(launcher, "cli", return_value="ok") as fake_cli:
            launcher.perform("upload", "COM7", "arm")
        self.assertEqual(launcher.job["state"], "error")
        self.assertEqual(fake_cli.call_count, 1)


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = launcher.ThreadingHTTPServer(("127.0.0.1", 0), launcher.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, method, path, headers=None, body=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        data = response.read()
        connection.close()
        return response.status, json.loads(data)

    def test_wrong_host_and_cross_origin_post_are_rejected(self):
        status, _ = self.request("GET", "/api/session", {"Host": "attacker.example"})
        self.assertEqual(status, 403)
        headers = {"Host": "localhost:49152", "Origin": "https://attacker.example", "Content-Type": "application/json", "X-Arm-Token": launcher.TOKEN}
        status, _ = self.request("POST", "/api/upload", headers, '{"port":"COM7","sketch":"arm"}')
        self.assertEqual(status, 403)

    def test_correct_origin_still_requires_token_and_valid_sketch(self):
        headers = {"Host": "localhost:49152", "Origin": "http://localhost:49152", "Content-Type": "application/json"}
        status, _ = self.request("POST", "/api/upload", headers, '{"port":"COM7","sketch":"arm"}')
        self.assertEqual(status, 403)
        headers["X-Arm-Token"] = launcher.TOKEN
        status, _ = self.request("POST", "/api/upload", headers, '{"port":"COM7","sketch":"other"}')
        self.assertEqual(status, 400)


if __name__ == "__main__":
    unittest.main()
