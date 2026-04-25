#!/usr/bin/env python3
"""
Serve the static web UI and proxy POST /api/step to ./solver_json (stdin JSON → stdout JSON).

Run from repository root:
  python3 web/server.py

Optional:
  SOLVER_JSON=/path/to/solver_json
  PORT=8765          # if busy, tries PORT+1 … up to +63 unless STRICT_PORT=1
  STRICT_PORT=1      # fail immediately if PORT is in use
"""

from __future__ import annotations

import errno
import json
import os
import subprocess
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent
WEB_ROOT = Path(__file__).resolve().parent
MAX_BODY = 2 * 1024 * 1024
SOLVER_TIMEOUT_SEC = 120
PORT_ATTEMPTS = 64


class ReuseThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


def solver_binary() -> str:
    env = os.environ.get("SOLVER_JSON")
    if env:
        return env
    return str(REPO_ROOT / "solver_json")


class Handler(BaseHTTPRequestHandler):
    server_version = "NerdleSolverWeb/1.0"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - - [%s] %s\n" % (self.client_address[0], self.log_date_time_string(), fmt % args))

    def _send(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
        self.send_response(status.value)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: HTTPStatus, obj: object) -> None:
        data = json.dumps(obj).encode("utf-8")
        self._send(status, data, "application/json; charset=utf-8")

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        if path == "/" or path == "":
            path = "/index.html"
        # strip leading /
        rel = path.lstrip("/")
        if ".." in rel.split("/"):
            self._send(HTTPStatus.FORBIDDEN, b"Forbidden", "text/plain; charset=utf-8")
            return
        fs_path = (WEB_ROOT / rel).resolve()
        try:
            fs_path.relative_to(WEB_ROOT.resolve())
        except ValueError:
            self._send(HTTPStatus.FORBIDDEN, b"Forbidden", "text/plain; charset=utf-8")
            return
        if not fs_path.is_file():
            self._send(HTTPStatus.NOT_FOUND, b"Not found", "text/plain; charset=utf-8")
            return
        data = fs_path.read_bytes()
        ctype = "application/octet-stream"
        if fs_path.suffix == ".html":
            ctype = "text/html; charset=utf-8"
        elif fs_path.suffix == ".css":
            ctype = "text/css; charset=utf-8"
        elif fs_path.suffix == ".js":
            ctype = "text/javascript; charset=utf-8"
        elif fs_path.suffix == ".svg":
            ctype = "image/svg+xml"
        elif fs_path.suffix == ".png":
            ctype = "image/png"
        self._send(HTTPStatus.OK, data, ctype)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/api/step":
            self._send(HTTPStatus.NOT_FOUND, b"Not found", "text/plain; charset=utf-8")
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length > MAX_BODY:
            self._send_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"ok": False, "error": "body too large"})
            return
        raw = self.rfile.read(length)
        try:
            raw.decode("utf-8")
        except UnicodeDecodeError:
            self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid UTF-8"})
            return

        bin_path = solver_binary()
        if not os.path.isfile(bin_path):
            self._send_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"ok": False, "error": f"solver_json not found at {bin_path} (run make solver_json)"},
            )
            return

        env = os.environ.copy()
        if "NERDLE_DATA_DIR" not in env:
            env["NERDLE_DATA_DIR"] = str(REPO_ROOT)

        try:
            proc = subprocess.run(
                [bin_path],
                input=raw,
                capture_output=True,
                timeout=SOLVER_TIMEOUT_SEC,
                env=env,
                cwd=str(REPO_ROOT),
            )
        except subprocess.TimeoutExpired:
            self._send_json(HTTPStatus.GATEWAY_TIMEOUT, {"ok": False, "error": "solver timeout"})
            return

        out = proc.stdout.decode("utf-8", errors="replace").strip()
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        if proc.returncode != 0:
            msg = err or out or f"solver exit {proc.returncode}"
            self._send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": msg})
            return
        try:
            obj = json.loads(out)
        except json.JSONDecodeError:
            self._send_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"ok": False, "error": "invalid JSON from solver", "raw": out[:500], "stderr": err[:500]},
            )
            return
        self._send_json(HTTPStatus.OK, obj)


def main() -> None:
    host = os.environ.get("HOST", "127.0.0.1")
    base_port = int(os.environ.get("PORT", "8765"))
    strict = os.environ.get("STRICT_PORT", "").strip() in ("1", "true", "yes", "on")
    last_err: OSError | None = None
    httpd: ReuseThreadingHTTPServer | None = None
    port = base_port
    attempts = 1 if strict else PORT_ATTEMPTS
    for i in range(attempts):
        port = base_port + i
        try:
            httpd = ReuseThreadingHTTPServer((host, port), Handler)
            break
        except OSError as e:
            last_err = e
            if e.errno != errno.EADDRINUSE or strict or i == attempts - 1:
                if strict and e.errno == errno.EADDRINUSE:
                    print(
                        f"Port {port} is already in use. Stop the other process, or run with "
                        f"PORT={port + 1} (or omit STRICT_PORT to auto-pick a free port).",
                        file=sys.stderr,
                    )
                raise
    assert httpd is not None
    if not strict and port != base_port:
        print(
            f"Port {base_port} was in use; listening on {port} instead "
            f"(set STRICT_PORT=1 to require {base_port}).",
            file=sys.stderr,
        )
    print(f"Serving http://{host}:{port}/  (API POST /api/step → {solver_binary()})", file=sys.stderr)
    print(f"Static root: {WEB_ROOT}", file=sys.stderr)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
