#!/usr/bin/env python3
"""Send a command to the ac-worldserver console and wait for its acknowledgement.

Linux counterpart to save_world.ps1. That script prefers SOAP and falls back to
tools/worldserver_cli.py, and both of those are Windows-only here (the helper
opens the Docker named pipe through ctypes.WinDLL), so this drives the console
the same way the helper does: attach over a pty, then leave with the detach
sequence.

Never close the container's stdin. `docker attach` hands its own stdin straight
to the worldserver console, which treats EOF as "halt" -- that is the failure
save_world.ps1 warns about. Detaching with ctrl-p ctrl-q ends the attach session
and leaves the console open.
"""

import argparse
import os
import pty
import select
import subprocess
import sys
import time

DETACH = b"\x10\x11"  # ctrl-p ctrl-q


def run(container: str, command: str, expect: str, timeout: float) -> int:
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["docker", "attach", "--detach-keys=ctrl-p,ctrl-q", container],
        stdin=slave, stdout=slave, stderr=slave, close_fds=True,
    )
    os.close(slave)

    captured = bytearray()
    seen = False
    try:
        time.sleep(1.0)  # let the attach settle before typing
        os.write(master, command.encode() + b"\n")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r, _, _ = select.select([master], [], [], 0.5)
            if not r:
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            captured += chunk
            if expect and expect.encode() in captured:
                seen = True
                break
    finally:
        try:
            os.write(master, DETACH)
            time.sleep(0.5)
        except OSError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.terminate()
        os.close(master)

    text = captured.decode("utf-8", "replace").strip()
    if text:
        print("--- console ---")
        print(text)
        print("--- end ---")

    if expect and not seen:
        print(f"FAIL: sent '{command}' but never saw '{expect}'")
        return 1
    print(f"OK: '{command}' acknowledged by {container}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--container", default="ac-worldserver")
    ap.add_argument("--command", default="saveall")
    ap.add_argument("--expect", default="All players saved")
    ap.add_argument("--timeout", type=float, default=25.0)
    args = ap.parse_args()

    running = subprocess.run(
        ["docker", "inspect", "-f", "{{.State.Running}}", args.container],
        capture_output=True, text=True,
    )
    if running.returncode != 0 or running.stdout.strip() != "true":
        print(f"OK: {args.container} is not running; nothing to send")
        return 0

    return run(args.container, args.command, args.expect, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
