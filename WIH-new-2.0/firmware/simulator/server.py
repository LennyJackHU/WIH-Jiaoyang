#!/usr/bin/env python3
"""Local standard-library HTTP server for the two-board simulator."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Optional
from urllib.parse import urlsplit

if __package__:
    from .model import Simulator
else:
    from model import Simulator


STATIC_DIR = Path(__file__).resolve().parent / "static"
STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
}
MAX_BODY = 16 * 1024


class RequestError(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"JSON 字段重复：{key}")
        result[key] = value
    return result


def create_server(host: str = "127.0.0.1", port: int = 8000, simulation: Optional[Simulator] = None):
    sim = simulation or Simulator()

    class Handler(BaseHTTPRequestHandler):
        server_version = "WIHSimulator/1.0"

        def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header(
                "Content-Security-Policy",
                "default-src 'self'; script-src 'self'; style-src 'self'; "
                "img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'",
            )
            self.end_headers()
            self.wfile.write(body)

        def _send_json(self, status: int, value) -> None:
            body = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self._send_bytes(status, "application/json; charset=utf-8", body)

        def _read_json(self):
            content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
            if content_type != "application/json":
                raise RequestError(415, "请求必须使用 application/json")
            raw_length = self.headers.get("Content-Length")
            if raw_length is None:
                raise RequestError(411, "缺少 Content-Length")
            try:
                length = int(raw_length)
            except ValueError as error:
                raise RequestError(400, "Content-Length 无效") from error
            if length < 0 or length > MAX_BODY:
                raise RequestError(413, "JSON 请求体过大")
            try:
                text = self.rfile.read(length).decode("utf-8")
                value = json.loads(text, object_pairs_hook=_unique_object)
            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                raise RequestError(400, f"JSON 无效：{error}") from error
            if not isinstance(value, dict):
                raise RequestError(400, "JSON 顶层必须是对象")
            return value

        @staticmethod
        def _fields(value, allowed, required=()):
            if set(value) - set(allowed) or not set(required).issubset(value):
                raise RequestError(400, f"字段必须且只能包含：{', '.join(allowed)}；必填：{', '.join(required)}")

        def do_GET(self) -> None:
            path = urlsplit(self.path).path
            if path == "/api/state":
                self._send_json(200, sim.state())
                return
            static = STATIC_FILES.get(path)
            if not static:
                self._send_json(404, {"error": "页面不存在"})
                return
            filename, content_type = static
            try:
                body = (STATIC_DIR / filename).read_bytes()
            except OSError:
                self._send_json(404, {"error": "静态资源不存在"})
                return
            self._send_bytes(200, content_type, body)

        def do_POST(self) -> None:
            try:
                path = urlsplit(self.path).path
                if path not in {"/api/action", "/api/candidate", "/api/connection", "/api/step", "/api/reset"}:
                    raise RequestError(404, "API 不存在")
                value = self._read_json()
                if path == "/api/action":
                    self._fields(value, ("board", "action", "steps"), ("board", "action"))
                    board = value["board"]
                    action = value["action"]
                    if not isinstance(board, str) or not isinstance(action, str):
                        raise RequestError(400, "board 和 action 必须是字符串")
                    if action == "encoder":
                        self._fields(value, ("board", "action", "steps"), ("board", "action", "steps"))
                    elif "steps" in value:
                        raise RequestError(400, "只有 encoder 可以携带 steps")
                    sim.action(board, action, value.get("steps"))
                elif path == "/api/candidate":
                    self._fields(value, ("board", "text"), ("board", "text"))
                    if not isinstance(value["board"], str) or value["board"] not in sim.boards:
                        raise RequestError(400, "board 只能是 A 或 B")
                    sim.set_candidate(value["board"], value["text"])
                elif path == "/api/connection":
                    self._fields(value, ("connected",), ("connected",))
                    sim.set_connected(value["connected"])
                else:
                    self._fields(value, ())
                    if path == "/api/step":
                        sim.step()
                    else:
                        sim.reset()
                if path != "/api/step":
                    sim.settle()
                self._send_json(200, sim.state())
            except RequestError as error:
                self._send_json(error.status, {"error": error.message})
            except ValueError as error:
                self._send_json(400, {"error": str(error)})
            except RuntimeError as error:
                self._send_json(409, {"error": str(error)})

        def log_message(self, format_string, *args) -> None:
            if not getattr(self.server, "quiet", False):
                super().log_message(format_string, *args)

    httpd = HTTPServer((host, port), Handler)
    httpd.simulator = sim
    return httpd


def _port(value: str) -> int:
    try:
        port = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("端口必须是整数") from error
    if not 0 <= port <= 65535:
        raise argparse.ArgumentTypeError("端口必须在 0 到 65535 之间")
    return port


def main() -> None:
    parser = argparse.ArgumentParser(description="WIH 双板句子编辑器 Web 模拟器")
    parser.add_argument("--port", type=_port, default=8000, help="监听端口，默认 8000；0 表示自动分配")
    args = parser.parse_args()
    httpd = create_server(port=args.port)
    host, port = httpd.server_address
    print(f"WIH 模拟器：http://{host}:{port}/ （仅监听本机，Ctrl+C 停止）", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n模拟器已停止")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
