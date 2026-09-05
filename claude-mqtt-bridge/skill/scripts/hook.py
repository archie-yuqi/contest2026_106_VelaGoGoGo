#!/usr/bin/env python3
"""Cross-platform Claude Code hook entrypoint and keepalive worker."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

from dependency import publisher

SCRIPT_DIR = Path(__file__).resolve().parent
PUBLISHER = SCRIPT_DIR / "publish_status.py"
PID_FILE = Path(os.environ.get("CLAUDE_MQTT_PID_FILE", "")) if os.environ.get("CLAUDE_MQTT_PID_FILE") else Path.home() / ".claude-code-status-keepalive.pid"
INTERVAL = max(1, int(os.environ.get("CLAUDE_MQTT_INTERVAL", "5")))


def ensure_dependency() -> bool:
    if publisher():
        return True
    helper = SCRIPT_DIR / "dependency.py"
    if os.environ.get("CLAUDE_MQTT_AUTO_INSTALL") == "1":
        try:
            completed = subprocess.run(
                [sys.executable, str(helper)],
                check=False,
                timeout=180,
            )
            if completed.returncode == 0 and publisher():
                return True
        except (OSError, subprocess.TimeoutExpired):
            pass
    subprocess.run([sys.executable, str(helper)], check=False, timeout=10)
    return False



def publish(message: str) -> None:
    try:
        subprocess.run(
            [sys.executable, str(PUBLISHER), message],
            check=False,
            timeout=4,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.TimeoutExpired):
        pass


def pid_alive(pid: int) -> bool:
    if os.name == "nt":
        try:
            completed = subprocess.run(
                ["tasklist", "/FI", f"PID eq {pid}"],
                check=False,
                capture_output=True,
                text=True,
                timeout=2,
            )
            return str(pid) in completed.stdout
        except (OSError, subprocess.TimeoutExpired):
            return False
    try:
        os.kill(pid, 0)
        return True
    except (OSError, ProcessLookupError):
        return False


def start() -> None:
    if PID_FILE.exists():
        try:
            old_pid = int(PID_FILE.read_text().strip())
        except (OSError, ValueError):
            old_pid = 0
        if old_pid and pid_alive(old_pid):
            return
        PID_FILE.unlink(missing_ok=True)

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    child = subprocess.Popen(
        [sys.executable, str(Path(__file__).resolve()), "keepalive"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=creationflags,
    )
    PID_FILE.write_text(str(child.pid), encoding="ascii")


def stop() -> None:
    if not PID_FILE.exists():
        return
    try:
        pid = int(PID_FILE.read_text().strip())
    except (OSError, ValueError):
        pid = 0
    if pid:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass
    PID_FILE.unlink(missing_ok=True)


def keepalive() -> None:
    running = True

    def handle_signal(signum, frame):
        nonlocal running
        running = False

    for signum in (getattr(signal, "SIGINT", None), getattr(signal, "SIGTERM", None)):
        if signum is not None:
            signal.signal(signum, handle_signal)

    while running:
        publish("online")
        time.sleep(INTERVAL)
    publish("offline")


def main() -> int:
    action = sys.argv[1] if len(sys.argv) > 1 else ""
    if action == "start":
        if not ensure_dependency():
            return 0
        start()
        publish("online")
        publish("idle")
    elif action == "end":
        stop()
        publish("offline")
    elif action == "keepalive":
        keepalive()
    elif action in {"idle", "thinking", "executing"}:
        publish(action)
    else:
        print("usage: hook.py start|end|idle|thinking|executing", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
