#!/usr/bin/env python3
"""Publish one Claude Code status/presence message over MQTT."""

from __future__ import annotations

import os
import subprocess
import sys

from dependency import publisher

BROKER = os.environ.get("CLAUDE_MQTT_BROKER", "test.mosquitto.org")
PORT = os.environ.get("CLAUDE_MQTT_PORT", "1883")
TOPIC = os.environ.get("CLAUDE_MQTT_TOPIC", "vela-go/claude-code/status/v1")
MESSAGES = {"idle", "thinking", "executing", "online", "offline"}


def publish(message: str) -> bool:
    if message not in MESSAGES:
        raise ValueError(f"invalid message: {message}")

    publisher_path = publisher()
    if not publisher_path:
        return False

    try:
        completed = subprocess.run(
            [
                publisher_path,
                "-h",
                BROKER,
                "-p",
                PORT,
                "-t",
                TOPIC,
                "-q",
                "0",
                "-m",
                message,
            ],
            check=False,
            timeout=3,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return completed.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in MESSAGES:
        print(
            "usage: publish_status.py idle|thinking|executing|online|offline",
            file=sys.stderr,
        )
        return 2
    publish(sys.argv[1])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
