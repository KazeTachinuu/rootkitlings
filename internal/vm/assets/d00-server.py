#!/usr/bin/env python3
"""Intentionally injectable status service used by the D00 deployment lab."""

from html import escape
from http.server import BaseHTTPRequestHandler, HTTPServer
import subprocess
import urllib.parse


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        host = query.get("host", ["localhost"])[0]
        result = subprocess.run(
            f"getent hosts {host}",
            shell=True,
            capture_output=True,
            text=True,
        )
        output = result.stdout + result.stderr

        if "text/html" in self.headers.get("Accept", ""):
            body = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Target status</title>
  <style>
    :root {{
      --bg: #f2eff8;
      --card: #fff;
      --ink: #1d1727;
      --muted: #746b82;
      --line: #ddd7e6;
      --purple: #7047eb;
      --purple-dark: #5530c9;
      --green: #159447;
      --terminal: #17131e;
      font: 16px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    }}
    * {{ box-sizing: border-box; }}
    body {{ margin: 0; background: var(--bg); color: var(--ink); }}
    main {{ width: min(820px, calc(100% - 32px)); margin: 8vh auto; }}
    header {{ display: flex; gap: 14px; align-items: center; margin-bottom: 28px; }}
    h1 {{ flex: 1; margin: 0; font: 750 clamp(1.5rem, 5vw, 2.2rem) system-ui,
      sans-serif; letter-spacing: -.03em; }}
    .tag {{ padding: 8px 11px; border: 2px solid var(--ink); background: var(--purple);
      color: #fff; box-shadow: 3px 3px 0 var(--ink); font-weight: 900; }}
    .badge {{ display: flex; gap: 8px; align-items: center; padding: 7px 10px;
      border: 1px solid var(--line); background: #ffffffaa; font: 800 .75rem system-ui,
      sans-serif; letter-spacing: .06em; }}
    .badge::before {{ width: 8px; height: 8px; border-radius: 50%;
      background: var(--green); content: ""; }}
    .card {{ overflow: hidden; border: 2px solid var(--ink); background: var(--card);
      box-shadow: 4px 4px 0 var(--ink); }}
    .head {{ padding: 11px 15px; border-bottom: 2px solid var(--ink); background: #faf9fc;
      font: 800 .75rem system-ui, sans-serif; letter-spacing: .09em;
      text-transform: uppercase; }}
    .body {{ padding: 18px; }}
    form {{ display: flex; gap: 10px; }}
    input, button {{ height: 44px; border: 2px solid var(--ink); font: inherit; }}
    input {{ flex: 1; min-width: 0; padding: 0 13px; outline: none; background: #fff;
      color: var(--ink); }}
    input:focus {{ border-color: var(--purple); box-shadow: 0 0 0 3px #7047eb20; }}
    button {{ padding: 0 20px; background: var(--purple); color: #fff;
      box-shadow: 2px 2px 0 var(--ink); cursor: pointer; font-weight: 800; }}
    button:hover {{ background: var(--purple-dark); }}
    button:active {{ box-shadow: none; transform: translate(2px, 2px); }}
    .label {{ margin: 20px 0 8px; color: var(--muted); font: 800 .68rem system-ui,
      sans-serif; letter-spacing: .09em; text-transform: uppercase; }}
    pre {{ min-height: 210px; margin: 0; padding: 18px; overflow: auto;
      background: var(--terminal); color: #e8e1f1; white-space: pre-wrap; }}
    footer {{ margin-top: 14px; color: var(--muted); font-size: .78rem; }}
    @media (max-width: 560px) {{
      main {{ margin: 28px auto; }}
      header {{ align-items: flex-start; flex-wrap: wrap; }}
      h1 {{ order: 3; flex-basis: 100%; }}
      form {{ align-items: stretch; flex-direction: column; }}
      button {{ align-self: flex-end; }}
    }}
  </style>
</head>
<body>
  <main>
    <header>
      <span class="tag">D00</span>
      <h1>Resolver status</h1>
      <span class="badge">ONLINE</span>
    </header>
    <section class="card">
      <div class="head">Target check</div>
      <div class="body">
        <form method="get">
          <input name="host" value="{escape(host)}" aria-label="Host"
            placeholder="Hostname or address" autocomplete="off" autofocus>
          <button type="submit">Resolve</button>
        </form>
        <div class="label">Command output</div>
        <pre>{escape(output or "No records found.")}</pre>
      </div>
    </section>
    <footer>status-check / target:8080</footer>
  </main>
</body>
</html>
"""
            content_type = "text/html; charset=utf-8"
        else:
            body = f"Status check: {host}\n{output}"
            content_type = "text/plain; charset=utf-8"

        payload = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        return


HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
