#!/usr/bin/env python3
# Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

"""A minimal Git HTTP server for the test suite.

Bridges requests to "git http-backend" so that the suite can exercise the real
smart HTTP protocol without reaching the network or needing an account. Prints
the port it bound to on the first line of standard output, so the caller can
let the operating system choose one.

Usage: githttpserver.py <repository-root> [user:password]
"""

import base64
import os
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = sys.argv[1]
CREDENTIALS = sys.argv[2] if len(sys.argv) > 2 else ""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        self.forward("GET")

    def do_POST(self):
        self.forward("POST")

    def authorized(self):
        if not CREDENTIALS:
            return True

        header = self.headers.get("Authorization", "")
        if header.startswith("Basic "):
            try:
                supplied = base64.b64decode(header[6:]).decode("utf-8")
            except (ValueError, UnicodeDecodeError):
                supplied = ""
            if supplied == CREDENTIALS:
                return True

        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="SquidyGit test"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def forward(self, method):
        if not self.authorized():
            return

        path, _, query = self.path.partition("?")
        environment = dict(os.environ)
        environment.update({
            "GIT_HTTP_EXPORT_ALL": "1",
            "GIT_PROJECT_ROOT": ROOT,
            "REQUEST_METHOD": method,
            "PATH_INFO": path,
            "QUERY_STRING": query,
            "REMOTE_ADDR": self.client_address[0],
            "CONTENT_TYPE": self.headers.get("Content-Type", ""),
        })

        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        if length:
            environment["CONTENT_LENGTH"] = str(length)

        finished = subprocess.run(["git", "http-backend"], input=body,
                                  capture_output=True, env=environment, check=False)

        head, _, payload = finished.stdout.partition(b"\r\n\r\n")
        status = 200
        headers = []
        for line in head.split(b"\r\n"):
            if not line:
                continue
            if line.lower().startswith(b"status:"):
                status = int(line.split()[1])
            else:
                headers.append(line)

        self.send_response(status)
        for line in headers:
            name, _, value = line.partition(b":")
            self.send_header(name.decode("latin-1"), value.strip().decode("latin-1"))
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        # Quiet: the suite reads standard output for the port.
        pass


def main():
    server = HTTPServer(("127.0.0.1", 0), Handler)
    print(server.server_port, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
