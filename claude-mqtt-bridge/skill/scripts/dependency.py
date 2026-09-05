#!/usr/bin/env python3
"""Check or optionally install the mosquitto publisher dependency."""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys


def publisher() -> str | None:
    configured = os.environ.get("CLAUDE_MQTT_PUB")
    if configured:
        configured_path = os.path.expanduser(configured)
        if os.path.isfile(configured_path) and os.access(configured_path, os.X_OK):
            return configured_path
        resolved = shutil.which(configured)
        if resolved:
            return resolved
    return shutil.which("mosquitto_pub") or shutil.which("mosquitto_pub.exe")


def install_command() -> list[str] | None:
    system = platform.system()
    if system == "Linux":
        if shutil.which("apt-get"):
            prefix = [] if os.geteuid() == 0 else ["sudo"]
            return prefix + ["apt-get", "update"]
        if shutil.which("dnf"):
            return ["sudo", "dnf", "install", "-y", "mosquitto-clients"]
        if shutil.which("pacman"):
            return ["sudo", "pacman", "-S", "--noconfirm", "mosquitto"]
    elif system == "Windows":
        if shutil.which("winget"):
            return ["winget", "install", "--id", "EclipseMosquitto.Mosquitto", "-e"]
        if shutil.which("choco"):
            return ["choco", "install", "mosquitto", "-y"]
    elif system == "Darwin" and shutil.which("brew"):
        return ["brew", "install", "mosquitto"]
    return None


def install() -> bool:
    system = platform.system()
    command = install_command()
    if not command:
        return False
    try:
        if system == "Linux" and command[-2:] == ["apt-get", "update"]:
            subprocess.run(command, check=True, timeout=120)
            install_cmd = command[:-2] + ["apt-get", "install", "-y", "mosquitto-clients"]
            subprocess.run(install_cmd, check=True, timeout=120)
        else:
            subprocess.run(command, check=True, timeout=120)
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return False
    return publisher() is not None


def guidance() -> str:
    system = platform.system()
    if system == "Linux":
        return "sudo apt-get update && sudo apt-get install -y mosquitto-clients"
    if system == "Windows":
        return "winget install --id EclipseMosquitto.Mosquitto -e"
    if system == "Darwin":
        return "brew install mosquitto"
    return "Install Eclipse Mosquitto and ensure mosquitto_pub is on PATH."


def main() -> int:
    if publisher():
        return 0
    if os.environ.get("CLAUDE_MQTT_AUTO_INSTALL") == "1" and install():
        return 0
    print(
        "Claude Code status MQTT dependency missing: mosquitto_pub.\n"
        f"Install it with: {guidance()}\n"
        "Or set CLAUDE_MQTT_PUB to the full mosquitto_pub path.\n"
        "Set CLAUDE_MQTT_AUTO_INSTALL=1 to let this helper attempt installation.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
