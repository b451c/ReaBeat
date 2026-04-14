"""TCP server for ReaBeat — handles beat detection requests from Lua.

Protocol: line-delimited JSON on localhost.
Commands: detect, ping, shutdown.
"""

from __future__ import annotations

import json
import logging
import socket
import threading
import time
from typing import Any, Dict, Optional

from reabeat.config import DEFAULT_PORT, DetectionConfig
from reabeat.detector import check_backend, detect_beats

logger = logging.getLogger("reabeat.server")


class ReaBeatServer:
    """Single-threaded TCP server for beat detection."""

    def __init__(self, port: int = DEFAULT_PORT, idle_timeout: int = 300):
        self.port = port
        self.idle_timeout = idle_timeout
        self._running = False
        self._last_activity = time.time()
        self._lock = threading.Lock()

    def serve(self) -> None:
        """Start server and accept connections until shutdown."""
        self._running = True
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.settimeout(5.0)
        srv.bind(("127.0.0.1", self.port))
        srv.listen(4)

        logger.info("ReaBeat server listening on port %d", self.port)

        # Idle timeout watchdog
        watchdog = threading.Thread(target=self._idle_watchdog, daemon=True)
        watchdog.start()

        while self._running:
            try:
                conn, addr = srv.accept()
                self._touch()
                threading.Thread(
                    target=self._handle_connection, args=(conn,), daemon=True
                ).start()
            except socket.timeout:
                continue
            except OSError:
                break

        srv.close()
        logger.info("ReaBeat server stopped.")

    def _touch(self) -> None:
        with self._lock:
            self._last_activity = time.time()

    def _idle_watchdog(self) -> None:
        """Shut down after idle_timeout seconds of inactivity."""
        while self._running:
            time.sleep(10)
            with self._lock:
                idle = time.time() - self._last_activity
            if idle > self.idle_timeout:
                logger.info("Idle timeout reached, shutting down.")
                self._running = False
                break

    def _handle_connection(self, conn: socket.socket) -> None:
        """Handle a single client connection."""
        conn.settimeout(300.0)
        buf = b""
        try:
            while self._running:
                data = conn.recv(8192)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self._touch()
                    self._handle_message(conn, line.decode("utf-8", errors="replace"))
        except (ConnectionResetError, BrokenPipeError, socket.timeout):
            pass
        finally:
            conn.close()

    def _handle_message(self, conn: socket.socket, raw: str) -> None:
        """Parse and dispatch a JSON command."""
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            self._send(conn, {"status": "error", "message": "Invalid JSON"})
            return

        cmd = msg.get("cmd", "")
        params = msg.get("params", {})

        if cmd == "ping":
            ok, msg = check_backend()
            self._send(conn, {"status": "ok", "result": {"pong": True, "backend_ok": ok, "backend_msg": msg}})
        elif cmd == "detect":
            self._handle_detect(conn, params)
        elif cmd == "shutdown":
            self._send(conn, {"status": "ok", "result": "shutting_down"})
            self._running = False
        else:
            self._send(conn, {"status": "error", "message": f"Unknown command: {cmd}"})

    def _handle_detect(self, conn: socket.socket, params: Dict[str, Any]) -> None:
        """Run beat detection and stream progress."""
        audio_path = params.get("audio_path")
        if not audio_path:
            self._send(conn, {"status": "error", "message": "Missing audio_path"})
            return

        config = DetectionConfig()

        def on_progress(message: str, fraction: float) -> None:
            self._send(conn, {
                "status": "progress",
                "message": message,
                "progress": round(fraction, 3),
            })

        try:
            result = detect_beats(audio_path, config=config, on_progress=on_progress)
            self._send(conn, {
                "status": "ok",
                "result": {
                    "beats": result.beats,
                    "downbeats": result.downbeats,
                    "tempo": result.tempo,
                    "time_sig_num": result.time_sig_num,
                    "time_sig_denom": result.time_sig_denom,
                    "confidence": result.confidence,
                    "backend": result.backend,
                    "duration": result.duration,
                    "peaks": result.peaks,
                    "detection_time": result.detection_time,
                },
            })
        except Exception as e:
            self._send(conn, {"status": "error", "message": str(e)})

    def _send(self, conn: socket.socket, data: Dict[str, Any]) -> None:
        """Send a JSON line to the client."""
        try:
            line = json.dumps(data, separators=(",", ":")) + "\n"
            conn.sendall(line.encode("utf-8"))
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
