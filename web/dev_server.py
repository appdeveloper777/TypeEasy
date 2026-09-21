import json
import subprocess
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


REPO_ROOT = Path(__file__).resolve().parent.parent
WEB_ROOT = REPO_ROOT / "web"

ALLOWED_ORIGINS = {"http://localhost:8001", "http://127.0.0.1:8001"}

ALLOWED_FILES = {
    "typeeasycode/apis/wasm_filter_endpoint.te",
    "typeeasycode/apis/dynamic_test_endpoint.te",
    "typeeasycode/apis/user_json_endpoint_endpoint.te",
    "typeeasycode/wasm/demo_suma.te",
}


def resolve_allowed_path(raw_path):
    if raw_path not in ALLOWED_FILES:
        raise ValueError("Archivo no permitido")

    path = (REPO_ROOT / raw_path).resolve()
    if not path.is_file():
        raise ValueError("El archivo no existe")
    if REPO_ROOT not in path.parents:
        raise ValueError("Ruta fuera del proyecto")

    return path


class TypeEasyDevHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_ROOT), **kwargs)

    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _enforce_same_origin(self):
        """Rejects cross-origin requests to the state-changing editor
        endpoints. A browser always sets Origin on cross-origin fetch/XHR
        (and on most same-origin POSTs too); only reject when it's present
        and doesn't match this server, so a malicious page open in another
        tab can't drive a CSRF request against the local dev editor."""
        origin = self.headers.get("Origin")
        if origin is not None and origin not in ALLOWED_ORIGINS:
            raise PermissionError(f"Origin no permitido: {origin}")

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != "/editor/file":
            return super().do_GET()

        try:
            self._enforce_same_origin()
            query = parse_qs(parsed.query)
            raw_path = query.get("path", [""])[0]
            file_path = resolve_allowed_path(raw_path)
            self.send_json(200, {
                "path": raw_path,
                "content": file_path.read_text(encoding="utf-8"),
            })
        except PermissionError as exc:
            self.send_json(403, {"error": str(exc)})
        except Exception as exc:
            self.send_json(400, {"error": str(exc)})

    def do_POST(self):
        parsed = urlparse(self.path)
        try:
            self._enforce_same_origin()
        except PermissionError as exc:
            return self.send_json(403, {"error": str(exc)})
        if parsed.path == "/editor/file":
            return self.save_file()
        if parsed.path == "/editor/restart-api":
            return self.restart_api()
        self.send_json(404, {"error": "Ruta no encontrada"})

    def save_file(self):
        try:
            content_type = (self.headers.get("Content-Type") or "").split(";")[0].strip()
            if content_type != "application/json":
                raise ValueError("Content-Type debe ser application/json")
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            raw_path = payload.get("path", "")
            content = payload.get("content", "")
            file_path = resolve_allowed_path(raw_path)
            file_path.write_text(content, encoding="utf-8")
            self.send_json(200, {"ok": True, "path": raw_path})
        except Exception as exc:
            self.send_json(400, {"error": str(exc)})

    def restart_api(self):
        try:
            result = subprocess.run(
                ["docker", "compose", "restart", "api"],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=60,
                check=False,
            )
            self.send_json(200, {
                "ok": result.returncode == 0,
                "code": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            })
        except Exception as exc:
            self.send_json(500, {"error": str(exc)})

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        super().end_headers()


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 8001), TypeEasyDevHandler)
    print("TypeEasy web editor: http://localhost:8001")
    print("Editables:")
    for path in sorted(ALLOWED_FILES):
        print(f"  - {path}")
    server.serve_forever()


if __name__ == "__main__":
    main()