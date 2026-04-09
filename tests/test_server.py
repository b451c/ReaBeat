"""Tests for REABeat server protocol."""

import json
import socket
import threading
import time

import pytest

from reabeat.server import REABeatServer


@pytest.fixture
def server_port():
    """Find a free port for testing."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture
def running_server(server_port):
    """Start a server in a background thread."""
    srv = REABeatServer(port=server_port, idle_timeout=30)
    thread = threading.Thread(target=srv.serve, daemon=True)
    thread.start()
    time.sleep(0.3)  # Give server time to bind
    yield srv, server_port
    srv._running = False
    thread.join(timeout=2)


def send_recv(port, msg):
    """Send a JSON message and receive response."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect(("127.0.0.1", port))
    sock.sendall((json.dumps(msg) + "\n").encode())
    data = b""
    while b"\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    sock.close()
    lines = data.decode().strip().split("\n")
    return [json.loads(line) for line in lines if line]


class TestServerProtocol:
    def test_ping(self, running_server):
        _, port = running_server
        responses = send_recv(port, {"cmd": "ping"})
        assert any(
            r.get("status") == "ok" and isinstance(r.get("result"), dict) and r["result"].get("pong")
            for r in responses
        )

    def test_unknown_command(self, running_server):
        _, port = running_server
        responses = send_recv(port, {"cmd": "nonexistent"})
        assert any(r.get("status") == "error" for r in responses)

    def test_invalid_json(self, running_server):
        _, port = running_server
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(("127.0.0.1", port))
        sock.sendall(b"not json\n")
        data = sock.recv(4096)
        sock.close()
        resp = json.loads(data.decode().strip())
        assert resp["status"] == "error"

    def test_detect_missing_path(self, running_server):
        _, port = running_server
        responses = send_recv(port, {"cmd": "detect", "params": {}})
        assert any(r.get("status") == "error" for r in responses)
