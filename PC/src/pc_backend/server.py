import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from pc_backend.diagnostics import (
        clear_all_dtcs,
        run_full_scan,
        run_function_test,
        scan_main_routed_ecus,
        scan_media_pi,
        test_main_routing_activation,
    )
else:
    from .diagnostics import (
        clear_all_dtcs,
        run_full_scan,
        run_function_test,
        scan_main_routed_ecus,
        scan_media_pi,
        test_main_routing_activation,
    )


HOST = "0.0.0.0"
PORT = 5000


class PcBackendHandler(BaseHTTPRequestHandler):
    server_version = "RVCR-PC-Backend/0.1"

    def do_OPTIONS(self):
        self._send_empty(204)

    def do_GET(self):
        route = urlparse(self.path)

        try:
            if route.path == "/health":
                self._send_json({"result": "OK"})
            elif route.path == "/diagnostics/media":
                self._send_json(scan_media_pi())
            elif route.path == "/diagnostics/main/routing-activation":
                self._send_json(test_main_routing_activation())
            elif route.path == "/diagnostics/main/ecus":
                self._send_json(scan_main_routed_ecus())
            elif route.path == "/diagnostics/run":
                self._send_json(run_full_scan())
            else:
                self._send_json({"result": "ERROR", "detail": "Not found"}, status=404)
        except Exception as exc:
            self._send_json({"result": "ERROR", "detail": str(exc)}, status=502)

    def do_POST(self):
        route = urlparse(self.path)

        try:
            if route.path == "/diagnostics/dtc/clear":
                self._send_json(clear_all_dtcs())
            elif route.path == "/diagnostics/routine":
                body = self._read_json_body()
                self._send_json(run_function_test(body.get("test_id", "")))
            else:
                self._send_json({"result": "ERROR", "detail": "Not found"}, status=404)
        except Exception as exc:
            self._send_json({"result": "ERROR", "detail": str(exc)}, status=502)

    def log_message(self, format, *args):
        return

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length == 0:
            return {}

        raw_body = self.rfile.read(length)
        return json.loads(raw_body.decode("utf-8"))

    def _send_empty(self, status):
        self.send_response(status)
        self._send_cors_headers()
        self.end_headers()

    def _send_json(self, payload, status=200):
        encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self._send_cors_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _send_cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")


def main():
    httpd = ThreadingHTTPServer((HOST, PORT), PcBackendHandler)
    print(f"PC backend listening on http://{HOST}:{PORT}")
    print("Diagnostic API: GET /diagnostics/run")
    httpd.serve_forever()


if __name__ == "__main__":
    main()