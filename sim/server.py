"""Local server of the browser simulator (Python standard library only).

Serves:
  /                 sim/web (page, main script, worker)
  /idfm_sim.wasm    the compiled app (built by scripts/sim.ps1)
  /sd/<path>        the simulated SD card = <repo>/sd/<path>
                    /sd/idfm/config.json is served with a placeholder API key: the real key never
                    reaches the browser, the proxy adds it.
  /proxy?u=<url>    forwards GET requests to the PRIM API only, with the real API key
  POST /shot?name=  saves a PNG screenshot posted by the page (automated captures)

Listens on 127.0.0.1 only. Sends the COOP/COEP headers needed for SharedArrayBuffer.
"""

import argparse
import json
import re
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB_DIR = ROOT / "sim" / "web"
SD_DIR = ROOT / "sd"
PRIM_BASE = "https://prim.iledefrance-mobilites.fr/"
KEY_PLACEHOLDER = "sim-proxy-key"
CONFIG_PATH = SD_DIR / "idfm" / "config.json"


def real_api_key():
    try:
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8")).get("apiKey", "")
    except (OSError, ValueError):
        return ""


class Handler(SimpleHTTPRequestHandler):
    wasm_path: Path = Path()
    shots_dir: Path = Path()
    quit_event: threading.Event = threading.Event()

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_DIR), **kwargs)

    def log_message(self, fmt, *args):  # quieter: only proxy calls and errors
        pass

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def guess_type(self, path):
        if str(path).endswith(".wasm"):
            return "application/wasm"
        if str(path).endswith(".js"):
            return "text/javascript"
        return super().guess_type(path)

    def send_bytes(self, code, data, ctype="application/octet-stream", extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    # ------------------------------------------------------------------ GET
    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path == "/idfm_sim.wasm":
            return self.send_bytes(200, self.wasm_path.read_bytes(), "application/wasm")
        if url.path.startswith("/sd/"):
            return self.serve_sd(urllib.parse.unquote(url.path[len("/sd/"):]))
        if url.path == "/proxy":
            return self.proxy(urllib.parse.parse_qs(url.query).get("u", [""])[0])
        return super().do_GET()

    def serve_sd(self, rel):
        target = (SD_DIR / rel).resolve()
        if SD_DIR.resolve() not in target.parents or not target.is_file():
            return self.send_bytes(404, b"not found", "text/plain")
        data = target.read_bytes()
        if target == CONFIG_PATH.resolve():
            try:
                cfg = json.loads(data.decode("utf-8"))
            except ValueError:
                return self.send_bytes(200, data, "application/json")
            key = cfg.get("apiKey", "")
            if key and not key.startswith("PUT_YOUR"):
                cfg["apiKey"] = KEY_PLACEHOLDER
            data = json.dumps(cfg).encode("utf-8")
        return self.send_bytes(200, data, "application/json")

    def proxy(self, target):
        if not target.startswith(PRIM_BASE):
            return self.send_bytes(403, b"only the PRIM API is proxied", "text/plain")
        headers = {}
        for name in ("apikey", "Accept"):
            value = self.headers.get(name)
            if value:
                headers[name] = value
        if headers.get("apikey") == KEY_PLACEHOLDER:
            headers["apikey"] = real_api_key()
        req = urllib.request.Request(target, headers=headers, method="GET")
        try:
            with urllib.request.urlopen(req, timeout=20) as resp:
                body, status, date = resp.read(), resp.status, resp.headers.get("Date", "")
        except urllib.error.HTTPError as err:
            body, status, date = err.read(), err.code, err.headers.get("Date", "")
        except (urllib.error.URLError, OSError) as err:
            print(f"[proxy] network error: {err}", flush=True)
            return self.send_bytes(502, str(err).encode(), "text/plain", {"X-Sim-Error": "1"})
        short = target[len(PRIM_BASE):].split("?")[0]
        print(f"[proxy] GET {short} -> {status} ({len(body)} bytes)", flush=True)
        extra = {"X-Upstream-Date": date} if date else {}
        return self.send_bytes(status, body, "application/json", extra)

    # ------------------------------------------------------------------ POST
    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path != "/shot":
            return self.send_bytes(404, b"not found", "text/plain")
        name = urllib.parse.parse_qs(url.query).get("name", ["shot.png"])[0]
        if not re.fullmatch(r"[\w.-]+\.png", name):
            return self.send_bytes(400, b"bad name", "text/plain")
        data = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.shots_dir.mkdir(parents=True, exist_ok=True)
        (self.shots_dir / name).write_bytes(data)
        print(f"[sim] screenshot saved: {self.shots_dir / name}", flush=True)
        self.send_bytes(200, b"ok", "text/plain")
        if urllib.parse.parse_qs(url.query).get("quit"):
            self.quit_event.set()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wasm", required=True, type=Path)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--shots-dir", type=Path, default=ROOT / ".tools" / "sim" / "shots")
    ap.add_argument("--open", default="", help="page query string to open in the default browser")
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--quit-after-shot", action="store_true", help="stop once a screenshot is posted with &quit=1")
    args = ap.parse_args()

    Handler.wasm_path = args.wasm.resolve()
    Handler.shots_dir = args.shots_dir.resolve()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/{args.open}"
    print(f"[sim] simulator on {url}  (Ctrl+C to stop)", flush=True)
    if not real_api_key() or real_api_key().startswith("PUT_YOUR"):
        print("[sim] no PRIM key in sd/idfm/config.json: live boards will show an error, the demo works", flush=True)
    if not args.no_browser:
        webbrowser.open(url)

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        if args.quit_after_shot:
            Handler.quit_event.wait()
        else:
            thread.join()
    except KeyboardInterrupt:
        pass
    server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
