#!/usr/bin/env python3
"""Vulnerable status page. Command injection via the 'host' parameter."""
from http.server import HTTPServer, BaseHTTPRequestHandler
import subprocess, urllib.parse

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        qs = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        host = qs.get("host", ["localhost"])[0]
        out = subprocess.run(f"ping -c1 -W1 {host}", shell=True,
                             capture_output=True, text=True)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(f"Status check: {host}\n{out.stdout}{out.stderr}".encode())

HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
