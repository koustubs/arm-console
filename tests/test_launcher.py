import http.client
import json
from pathlib import Path
import sys
import threading
import time
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

    def test_unexpected_worker_error_does_not_leave_job_running(self):
        with patch.object(launcher, "ports", side_effect=TypeError("invalid discovery data")):
            launcher.perform("upload", "COM7", "arm")
        self.assertEqual(launcher.job["state"], "error")
        self.assertIn("invalid discovery data", launcher.job["output"])


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

    def setUp(self):
        launcher.job.update(state="idle", output="")

    def test_dashboard_files_are_not_cached(self):
        for path in ("/", "/app.js", "/style.css", "/api/session"):
            with self.subTest(path=path):
                connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
                connection.request("GET", path, headers={"Host": "localhost:49152"})
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.getheader("Cache-Control"), "no-store")
                response.read()
                connection.close()

    def wait_for_job(self):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            status, result = self.request("GET", "/api/job", {"Host": "localhost:49152"})
            self.assertEqual(status, 200)
            if result["state"] != "running":
                return result
            time.sleep(0.01)
        self.fail("Job did not finish")

    def test_full_upload_request_compiles_then_verifies(self):
        calls = []
        def fake_cli(*args, **kwargs):
            calls.append(args)
            return "ok"
        headers = {"Host": "localhost:49152", "Origin": "http://localhost:49152", "Content-Type": "application/json", "X-Arm-Token": launcher.TOKEN}
        port = {"address": "COM7", "label": "COM7", "boards": ["Arduino Uno"]}
        with patch.object(launcher, "ports", return_value=[port]), patch.object(launcher, "cli", side_effect=fake_cli):
            status, available = self.request("GET", "/api/ports", {"Host": "localhost:49152"})
            self.assertEqual(status, 200)
            self.assertEqual(available["ports"], [port])
            status, _ = self.request("POST", "/api/upload", headers, '{"port":"COM7","sketch":"original"}')
            self.assertEqual(status, 202)
            self.assertEqual(self.wait_for_job()["state"], "success")
        self.assertEqual([call[0] for call in calls], ["compile", "upload"])
        self.assertIn("--verify", calls[1])
        self.assertTrue(any("Robotic_Arm_Controller" in arg for arg in calls[1]))

    def test_setup_request_reports_failed_install(self):
        headers = {"Host": "localhost:49152", "Origin": "http://localhost:49152", "Content-Type": "application/json", "X-Arm-Token": launcher.TOKEN}
        with patch.object(launcher, "cli", side_effect=RuntimeError("network unavailable")):
            status, _ = self.request("POST", "/api/setup", headers, '{}')
            self.assertEqual(status, 202)
            result = self.wait_for_job()
        self.assertEqual(result["state"], "error")
        self.assertIn("network unavailable", result["output"])

    def test_port_discovery_failure_returns_an_error_response(self):
        with patch.object(launcher, "ports", side_effect=TypeError("invalid discovery data")):
            status, result = self.request("GET", "/api/ports", {"Host": "localhost:49152"})
        self.assertEqual(status, 500)
        self.assertIn("invalid discovery data", result["error"])

    def test_second_job_is_rejected_while_first_is_running(self):
        entered = threading.Event()
        release = threading.Event()
        def blocked(*args):
            entered.set()
            release.wait(3)
            with launcher.lock:
                launcher.job.update(state="success", output="done")
        headers = {"Host": "localhost:49152", "Origin": "http://localhost:49152", "Content-Type": "application/json", "X-Arm-Token": launcher.TOKEN}
        with patch.object(launcher, "perform", side_effect=blocked):
            try:
                status, _ = self.request("POST", "/api/setup", headers, '{}')
                self.assertEqual(status, 202)
                self.assertTrue(entered.wait(2))
                status, _ = self.request("POST", "/api/upload", headers, '{"port":"COM7","sketch":"arm"}')
                self.assertEqual(status, 409)
            finally:
                release.set()
            self.assertEqual(self.wait_for_job()["state"], "success")

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
