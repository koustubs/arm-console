"""Local dashboard and narrowly scoped Arduino Uno upload service."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import secrets
import shutil
import subprocess
import threading
import webbrowser

ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / "arduino-cli.yaml"
TOKEN = secrets.token_urlsafe(32)
HOSTS = {"localhost:49152", "127.0.0.1:49152"}
ORIGINS = {"http://localhost:49152", "http://127.0.0.1:49152"}
lock = threading.Lock()
job = {"state": "idle", "output": ""}


def cli(*args, timeout=600):
    executable = shutil.which("arduino-cli")
    if not executable:
        raise RuntimeError("Arduino CLI not found. Install Arduino CLI and restart Arm Console.")
    result = subprocess.run([executable, *args, "--config-file", str(CONFIG)], cwd=ROOT,
                            capture_output=True, text=True, errors="replace", timeout=timeout,
                            shell=False)
    if result.returncode:
        raise RuntimeError((result.stderr + "\n" + result.stdout).strip()[-12000:])
    return result.stdout


def ports():
    data = json.loads(cli("board", "list", "--format", "json", timeout=30))
    entries = data.get("detected_ports", []) if isinstance(data, dict) else data
    return [{"address": item["port"]["address"],
             "label": item["port"].get("label", item["port"]["address"]),
             "boards": [board.get("name", "") for board in item.get("matching_boards", [])]}
            for item in entries if item.get("port", {}).get("protocol") == "serial"
            and item["port"].get("address")]


def perform(action, port=None, sketch=None):
    try:
        if action == "setup":
            output = cli("core", "update-index") + cli("core", "install", "arduino:avr")
            output += cli("lib", "install", "Servo")
        else:
            if port not in [p["address"] for p in ports()]:
                raise RuntimeError("The selected port is no longer available. Refresh the list.")
            choices = {"arm": ROOT / "firmware" / "ArmController",
                       "original": ROOT / "baseline" / "Robotic_Arm_Controller"}
            target = choices[sketch]
            build = ROOT / ".arduino" / "build" / sketch
            build.mkdir(parents=True, exist_ok=True)
            output = cli("compile", "--fqbn", "arduino:avr:uno", "--build-path", str(build), str(target))
            # Recheck the selected port after compilation; no programmer or bootloader commands.
            if port not in [p["address"] for p in ports()]:
                raise RuntimeError("The selected port disappeared during compilation; upload was not started.")
            output += "\n" + cli("upload", "--fqbn", "arduino:avr:uno", "--port", port,
                                  "--input-dir", str(build), "--verify", str(target))
        with lock:
            job.update(state="success", output=output[-12000:])
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError, KeyError) as error:
        with lock:
            job.update(state="error", output=str(error)[-12000:])


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT / "web"), **kwargs)

    def valid_host(self):
        return self.headers.get("Host", "") in HOSTS

    def respond(self, code, value):
        payload = json.dumps(value).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if not self.valid_host():
            return self.respond(403, {"error": "Invalid host"})
        if self.path == "/api/session":
            return self.respond(200, {"token": TOKEN})
        if self.path == "/api/job":
            with lock:
                snapshot = dict(job)
            return self.respond(200, snapshot)
        if self.path == "/api/ports":
            try:
                with lock:
                    if job["state"] == "running":
                        return self.respond(409, {"error": "A job is running."})
                    result = ports()
                return self.respond(200, {"ports": result})
            except (OSError, RuntimeError, subprocess.SubprocessError, ValueError, KeyError) as error:
                return self.respond(500, {"error": str(error)})
        return super().do_GET()

    def do_POST(self):
        if not self.valid_host() or self.headers.get("Origin") not in ORIGINS or not secrets.compare_digest(self.headers.get("X-Arm-Token", ""), TOKEN):
            return self.respond(403, {"error": "Request rejected"})
        if self.path not in ("/api/setup", "/api/upload") or self.headers.get("Content-Type") != "application/json":
            return self.respond(400, {"error": "Invalid request"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length < 2 or length > 4096:
                raise ValueError("Invalid request size")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Invalid request body")
            action = "setup" if self.path == "/api/setup" else "upload"
            port, sketch = body.get("port"), body.get("sketch")
            if action == "upload" and (not isinstance(port, str) or not port or len(port) > 256 or sketch not in ("arm", "original")):
                raise ValueError("Choose a listed serial port and a sketch")
        except (ValueError, TypeError):
            return self.respond(400, {"error": "Invalid request"})
        with lock:
            if job["state"] == "running":
                return self.respond(409, {"error": "A job is already running"})
            job.update(state="running", output="")
        threading.Thread(target=perform, args=(action, port, sketch), daemon=True).start()
        self.respond(202, {"state": "running"})


def main():
    try:
        with ThreadingHTTPServer(("127.0.0.1", 49152), Handler) as server:
            url = "http://localhost:49152"
            print(f"Open {url} in Chrome or Edge. Keep this window open; Ctrl+C stops the server.", flush=True)
            webbrowser.open(url)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
    except OSError as error:
        print(f"Could not start the local server: {error}\nClose another app using port 49152 and try again.")


if __name__ == "__main__":
    main()
