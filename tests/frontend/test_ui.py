#!/usr/bin/env python3
"""Frontend HTTP and real Firefox checks. Python stdlib; no npm/Selenium.

Run: python3 tests/frontend/test_ui.py [--docker-bridge] [--http-only]
Needs Firefox + geckodriver for browser checks. FRONTEND_URL defaults to
http://127.0.0.1:8000. --docker-bridge uses a temporary localhost test proxy
through `docker compose exec frontend nc` when a VPN blocks Docker's host port;
it tests the real Nginx/UI/API, but NOT the host port publication itself.
"""

import argparse
import base64
import http.client
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def fetch(url, method="GET", data=None):
    payload = None if data is None else json.dumps(data).encode()
    request = urllib.request.Request(url, data=payload, method=method,
                                     headers={"Content-Type": "application/json"})
    try:
        response = HTTP.open(request, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, response.headers, response.read()


class Bridge(http.server.BaseHTTPRequestHandler):
    """A test-only transport, never part of deployment or application code."""

    def log_message(self, *_):
        pass

    def do_GET(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        headers = "".join(f"{key}: {value}\r\n" for key, value in self.headers.items()
                          if key.lower() not in {"host", "connection", "accept-encoding"})
        raw = (f"{self.command} {self.path} HTTP/1.1\r\nHost: localhost:8080\r\n"
               f"Connection: close\r\n{headers}\r\n").encode() + body
        # Keep stdin open until the response arrives. Closing it immediately
        # makes BusyBox nc close the socket while an asynchronous POST runs.
        with subprocess.Popen(
            ["docker", "compose", "exec", "-T", "frontend", "nc", "-w", "15", "127.0.0.1", "8080"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            cwd=ROOT) as process:
            process.stdin.write(raw)
            process.stdin.flush()

            class RawSocket:
                def makefile(self, *_):
                    return process.stdout

            response = http.client.HTTPResponse(RawSocket())
            response.begin()
            decoded = response.read()
            process.stdin.close()
            process.wait(timeout=20)
        self.send_response(response.status)
        for key, value in response.getheaders():
            if key.lower() not in {"connection", "transfer-encoding", "content-length"}:
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(decoded)))
        self.end_headers()
        self.wfile.write(decoded)

    do_POST = do_GET


class Browser:
    def __init__(self, address):
        self.address = address
        self.session = self.call("POST", "/session", {"capabilities": {"alwaysMatch": {
            "browserName": "firefox", "moz:firefoxOptions": {
                "args": ["-headless"], "prefs": {"network.proxy.type": 0},
            },
        }}})["sessionId"]

    def call(self, method, path, data=None):
        status, _, body = fetch(self.address + path, method, data)
        value = json.loads(body)["value"]
        if status >= 400:
            raise AssertionError(value.get("message", "WebDriver command failed"))
        return value

    def command(self, method, path, data=None):
        return self.call(method, f"/session/{self.session}{path}", data)

    def js(self, script):
        return self.command("POST", "/execute/sync", {"script": script, "args": []})

    def wait(self, expression, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.js("return " + expression):
                return
            time.sleep(0.15)
        raise AssertionError("Browser condition timed out: " + expression)

    def element(self, css):
        value = self.command("POST", "/element", {"using": "css selector", "value": css})
        return value["element-6066-11e4-a52e-4f735466cecf"]

    def click(self, css):
        self.wait(f"document.querySelector({json.dumps(css)})?.disabled !== true")
        self.command("POST", f"/element/{self.element(css)}/click", {})

    def fill(self, css, text):
        element = self.element(css)
        self.command("POST", f"/element/{element}/clear", {})
        self.command("POST", f"/element/{element}/value", {"text": text})

    def new_tab(self, url):
        handle = self.command("POST", "/window/new", {"type": "tab"})["handle"]
        self.switch(handle)
        self.command("POST", "/url", {"url": url})
        self.wait("document.querySelectorAll('.cell').length === 64")
        return handle

    def switch(self, handle):
        self.command("POST", "/window", {"handle": handle})


def http_checks(url):
    status, headers, page = fetch(url + "/")
    assert status == 200 and b'id="board"' in page and b'/app.js' in page
    assert headers["X-Content-Type-Options"] == "nosniff"
    assert "frame-ancestors 'none'" in headers["Content-Security-Policy"]
    assert headers["Cache-Control"] == "no-store"
    for path, mime in [("/app.js", "javascript"), ("/style.css", "text/css")]:
        status, headers, body = fetch(url + path)
        assert status == 200 and mime in headers["Content-Type"] and body
    assert json.loads(fetch(url + "/api/hello")[2]) == {"message": "hello-world"}
    assert json.loads(fetch(url + "/hello")[2]) == {"message": "hello-world"}
    assert fetch(url + "/missing")[0] == 404
    assert fetch(url + "/nginx.conf")[0] == 404
    created = json.loads(fetch(url + "/api/games", "POST")[2])
    game = created["game_id"]
    assert fetch(url + f"/api/games/{game}/moves", "POST",
                 {"from": [5, 0], "to": [4, 1]})[0] == 401
    print("PASS HTTP: static assets, security headers, proxy, 404 and move authorization")


def trace_requests(browser):
    # Test-only fetch instrumentation: no app hooks, tokens or headers recorded.
    browser.js("""
      window.__originalFetch ??= window.fetch;
      const p = window.__pollProbe = {
        gets: 0, posts: 0, active: 0, max: 0, fail: false, reply: null,
        delay: 0, hiddenAt: null, hiddenGets: null
      };
      window.fetch = async (url, options) => {
        const method = options?.method || 'GET';
        p.active++; p.max = Math.max(p.max, p.active);
        try {
          if (method === 'GET') {
            p.gets++;
            if (p.delay) await new Promise(resolve => setTimeout(resolve, p.delay));
            if (p.fail) throw new TypeError('Injected offline error');
            if (p.reply) {
              const reply = p.reply; p.reply = null;
              return new Response(JSON.stringify(reply), {status: 200});
            }
          } else p.posts++;
          return await window.__originalFetch(url, options);
        } finally { p.active--; }
      };
      if (!window.__visibilityProbe) {
        document.addEventListener('visibilitychange', () => {
          const probe = window.__pollProbe;
          if (document.hidden) probe.hiddenAt = probe.gets;
          else if (probe.hiddenAt !== null) probe.hiddenGets = probe.gets - probe.hiddenAt;
        }, true);
        window.__visibilityProbe = true;
      }
    """)


def seed_position(game, pieces, turn="white"):
    # Arrange only a NEW game created by this browser test; there is no
    # position-editing endpoint in the application.
    board = ["."] * 64
    for row, col, piece in pieces:
        assert (row + col) % 2 and piece in "wbWB"
        board[row * 8 + col] = piece
    subprocess.run(["docker", "compose", "exec", "-T", "postgres", "psql", "-X",
                    "-v", "ON_ERROR_STOP=1", "-U", "mini_checkers", "-d", "mini_checkers",
                    "-c", f"UPDATE games SET board = '{''.join(board)}', current_turn = '{turn}', "
                    f"version = version + 1 WHERE id = '{game}'"],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["docker", "compose", "exec", "-T", "redis", "redis-cli", "DEL", f"game:{game}"],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def capture_checks(b, url):
    white = b.new_tab(url)
    b.click("#create")
    b.wait("document.querySelector('#game-status').textContent === 'Ожидание игрока'")
    game = b.js("return document.querySelector('#game-id').value")
    black = b.new_tab(url)
    b.fill("#connect-id", game)
    b.click("#join")
    b.wait("document.querySelector('#player-side').textContent === 'Чёрные'")
    seed_position(game, [(2, 1, "w"), (5, 0, "w"), (1, 2, "b"), (3, 6, "b")])
    b.switch(white)
    b.wait("document.querySelector('#game-version').textContent === '3'")
    assert b.js("return document.querySelector('[data-row=\"5\"][data-col=\"0\"]').disabled")
    b.click('[data-row="2"][data-col="1"]')
    b.click('[data-row="0"][data-col="3"]')
    b.wait("document.querySelector('#turn-badge').textContent === 'Продолжите бой'")
    assert b.js("return document.querySelectorAll('.piece.king').length === 1 && document.querySelectorAll('.cell.captured').length === 1")
    b.command("POST", "/refresh", {})
    b.wait("document.querySelector('#turn-badge').textContent === 'Продолжите бой'")
    assert b.js("return document.querySelector('.selected').dataset.row === '0' && document.querySelector('.selected').dataset.col === '3'")
    assert b.js("return document.querySelector('[data-row=\"5\"][data-col=\"0\"]').disabled")
    Path("/tmp/opencode/t01-capture.png").write_bytes(base64.b64decode(b.command("GET", "/screenshot")))
    b.click('[data-row="4"][data-col="7"]')
    b.wait("document.querySelector('#game-status').textContent === 'Завершена'")
    assert b.js("return document.querySelector('#turn-status').textContent === 'Победили белые' && document.querySelectorAll('.cell.captured').length === 0")
    b.switch(black)
    b.wait("document.querySelector('#game-status').textContent === 'Завершена'")
    print("PASS captures UI: compulsory capture, promotion during chain, forced selection after reload, ghost removal, automatic win")

    b.switch(white)
    b.click("#create")
    b.wait("document.querySelector('#game-status').textContent === 'Ожидание игрока'")
    game = b.js("return document.querySelector('#game-id').value")
    b.switch(black)
    b.fill("#connect-id", game)
    b.click("#join")
    b.wait("document.querySelector('#game-id').value === " + json.dumps(game))
    seed_position(game, [(6, 1, "b"), (4, 7, "w")], "black")
    b.click("#refresh")
    b.wait("document.querySelector('#game-version').textContent === '3'")
    b.click('[data-row="6"][data-col="1"]')
    b.click('[data-row="7"][data-col="0"]')
    b.wait("document.querySelector('[data-row=\"7\"][data-col=\"0\"] .piece.black.king') !== null")
    b.switch(white)
    b.wait("document.querySelector('#game-version').textContent === '4'")
    b.click('[data-row="4"][data-col="7"]')
    b.click('[data-row="3"][data-col="6"]')
    b.wait("document.querySelector('#game-version').textContent === '5'")
    b.switch(black)
    b.wait("document.querySelector('#game-version').textContent === '5'")
    b.click('[data-row="7"][data-col="0"]')
    b.click('[data-row="4"][data-col="3"]')
    b.wait("document.querySelector('[data-row=\"4\"][data-col=\"3\"] .piece.black.king') !== null")
    print("PASS captures UI: black promotion and distant king move through the real API")


def browser_checks(browser, url, screenshot):
    b = browser
    white = b.new_tab(url)
    b.fill("#connect-id", "not-a-game")
    b.click("#join")
    b.wait("document.querySelector('#message').classList.contains('error')")
    b.click("#create")
    b.wait("document.querySelector('#game-status').textContent === 'Ожидание игрока'")
    game = b.js("return document.querySelector('#game-id').value")
    assert b.js("return document.querySelectorAll('.piece').length") == 24
    assert b.js("return document.querySelectorAll('.cell:not(:disabled)').length") == 0
    assert b.js("return document.querySelector('#player-side').textContent") == "Белые"
    assert b.js("return !document.body.textContent.includes(JSON.parse(sessionStorage.getItem('mini-checkers:player:' + document.querySelector('#game-id').value)).player_token) && !location.href.includes('token') && localStorage.length === 0")
    print("PASS UI: invalid ID, create, initial board, waiting state, credentials private")

    # Use the UI's noopener link: the new tab must not inherit white's key.
    handles = b.command("GET", "/window/handles")
    b.click('a[target="_blank"]')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        added = set(b.command("GET", "/window/handles")) - set(handles)
        if added:
            break
        time.sleep(0.15)
    assert added
    black = added.pop()
    b.switch(black)
    b.wait("document.querySelectorAll('.cell').length === 64")
    assert b.js("return sessionStorage.length") == 0
    b.fill("#connect-id", game)
    b.click("#join")
    b.wait("document.querySelector('#player-side').textContent === 'Чёрные'")
    print("PASS UI: second tab has no inherited key and joins as black")

    b.switch(white)
    b.wait("document.querySelector('#turn-badge').textContent === 'Ваш ход'")
    trace_requests(b)
    b.click('[data-row="5"][data-col="0"]')
    assert b.js("return document.querySelectorAll('.cell.legal').length") == 1
    b.js("window.__selectedCell = document.querySelector('.selected')")
    b.fill("#connect-id", "draft")
    b.wait("window.__pollProbe.gets >= 1 && window.__pollProbe.active === 0")
    assert b.js("return document.querySelector('.selected') === window.__selectedCell && document.querySelector('#connect-id').value === 'draft'")
    b.fill("#connect-id", game)
    print("PASS polling: unchanged state preserves selected piece, DOM and form input")

    # Start a manual refresh during a deliberately delayed automatic GET.
    b.js("window.__pollProbe.delay = 400")
    b.wait("window.__pollProbe.active === 1")
    b.click("#refresh")
    b.wait("!document.querySelector('#refresh').disabled && window.__pollProbe.active === 0")
    assert b.js("return window.__pollProbe.max") == 1
    b.js("window.__pollProbe.delay = 0")
    older_state = json.loads(fetch(url + f"/api/games/{game}")[2])
    screenshot.write_bytes(base64.b64decode(b.command("GET", "/screenshot")))
    b.click('[data-row="4"][data-col="1"]')
    b.wait("document.querySelector('#game-version').textContent === '3'")
    assert b.js("return document.querySelectorAll('.cell:not(:disabled)').length") == 0
    b.js("window.__pollProbe.reply = " + json.dumps(older_state))
    b.wait("window.__pollProbe.reply === null && window.__pollProbe.active === 0")
    assert b.js("return document.querySelector('#game-version').textContent") == "3"
    assert b.js("return window.__pollProbe.max === 1 && window.__pollProbe.posts === 1")
    print("PASS polling: requests serialized, POST not retried, older cached version ignored")

    b.switch(black)
    b.wait("document.querySelector('#turn-badge').textContent === 'Ваш ход'")
    b.click('[data-row="2"][data-col="1"]')
    b.click('[data-row="3"][data-col="0"]')
    b.wait("document.querySelector('#game-version').textContent === '4'")
    time.sleep(3)
    b.switch(white)
    b.wait("document.querySelector('#game-version').textContent === '4'")
    assert b.js("return window.__pollProbe.hiddenGets") == 0
    b.command("POST", "/refresh", {})
    b.wait("document.querySelector('#game-id').value !== '' && !document.querySelector('#refresh').disabled")
    assert b.js("return document.querySelector('#player-side').textContent") == "Белые"
    b.wait("document.querySelector('#turn-badge').textContent === 'Ваш ход'")
    print("PASS polling: join and both moves arrive automatically, hidden tab pauses, return refreshes; reload preserves white")

    b.new_tab(url)
    b.fill("#connect-id", game)
    b.click("#open")
    b.wait("document.querySelector('#player-side').textContent === 'Зритель'")
    assert b.js("return document.querySelector('#finish').disabled && document.querySelectorAll('.cell:not(:disabled)').length === 0")
    b.click("#join")
    b.wait("document.querySelector('#message').classList.contains('error')")
    assert b.js("return document.querySelector('#player-side').textContent") == "Зритель"
    print("PASS UI: spectator cannot move/resign; third join fails without changing role")

    b.switch(black)
    b.click("#finish")
    b.command("POST", "/alert/accept", {})
    b.wait("document.querySelector('#game-status').textContent === 'Завершена'")
    assert b.js("return document.querySelector('#turn-status').textContent") == "Победили белые"
    b.switch(white)
    b.wait("document.querySelector('#game-status').textContent === 'Завершена'")
    assert b.js("return document.querySelectorAll('.cell:not(:disabled)').length === 0")
    trace_requests(b)
    time.sleep(3)
    assert b.js("return window.__pollProbe.gets") == 0
    print("PASS polling: resignation arrives automatically and finished game stops polling")

    b.click("#create")
    b.wait("document.querySelector('#game-status').textContent === 'Ожидание игрока'")
    trace_requests(b)
    b.js("window.__pollProbe.fail = true")
    b.wait("document.querySelector('#sync-status').textContent.includes('Повтор через 4 с')")
    count = b.js("return window.__pollProbe.gets")
    time.sleep(2.2)
    assert b.js("return window.__pollProbe.gets") == count
    assert b.js("return document.querySelector('#finish').disabled")
    b.js("window.__pollProbe.fail = false")
    b.wait("!document.querySelector('#finish').disabled && document.querySelector('#sync-status').textContent.includes('каждые 2 секунды')")
    assert b.js("return window.__pollProbe.posts") == 0
    print("PASS polling: injected network error backs off, marks stale state and recovers with GET only")
    b.click("#finish")
    b.command("POST", "/alert/accept", {})
    b.wait("document.querySelector('#turn-status').textContent === 'Игра закрыта'")
    b.command("POST", "/window/rect", {"width": 390, "height": 844})
    assert b.js("return document.documentElement.scrollWidth <= window.innerWidth")
    print("PASS UI: close waiting game, narrow viewport has no horizontal overflow")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docker-bridge", action="store_true")
    parser.add_argument("--http-only", action="store_true")
    args = parser.parse_args()
    os.chdir(ROOT)
    server = None
    driver = None
    browser = None
    try:
        url = os.environ.get("FRONTEND_URL", "http://127.0.0.1:8000").rstrip("/")
        if args.docker_bridge:
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Bridge)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            url = f"http://127.0.0.1:{server.server_port}"
            print("INFO Docker test bridge active; the published host port is NOT tested")
        http_checks(url)
        if args.http_only:
            return
        if not shutil.which("geckodriver"):
            raise SystemExit("Browser checks need Firefox and geckodriver (or use --http-only)")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        address = f"http://127.0.0.1:{port}"
        with tempfile.TemporaryFile() as log:
            driver = subprocess.Popen(["geckodriver", "--host", "127.0.0.1", "--port", str(port)],
                                      stdout=log, stderr=log)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                try:
                    if fetch(address + "/status")[0] == 200:
                        break
                except OSError:
                    time.sleep(0.2)
            browser = Browser(address)
            browser.command("POST", "/window/rect", {"width": 1280, "height": 1100})
            screenshot = Path(os.environ.get("UI_SCREENSHOT", "/tmp/opencode/t01-ui.png"))
            browser_checks(browser, url, screenshot)
            capture_checks(browser, url)
            print(f"PASS browser scenarios; screenshot: {screenshot}")
    finally:
        if browser:
            browser.command("DELETE", "", None)
        if driver:
            driver.terminate()
            driver.wait(timeout=15)
        if server:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
