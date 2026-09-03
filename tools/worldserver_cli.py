# Send one AzerothCore console line to a TTY docker worldserver via the Engine attach API.
# docker CLI attach refuses piped stdin ("stdin is not a terminal"). Named-pipe attach works.
# OpenStdin stays open after this client disconnects; --sig-proxy is not involved.
# Do not print credentials.

from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.WriteFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPCVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.PeekNamedPipe.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.PeekNamedPipe.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.SetNamedPipeHandleState.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.SetNamedPipeHandleState.restype = wintypes.BOOL


def docker_pipe_path() -> str:
    raw = subprocess.check_output(
        ["docker", "context", "inspect", "-f", "{{.Endpoints.docker.Host}}"],
        text=True,
    ).strip()
    if raw.startswith("npipe:"):
        name = raw.rstrip("/").split("/")[-1]
        return os.path.join("\\\\.\\pipe", name)
    if raw.startswith("unix://"):
        raise RuntimeError("unix docker socket is not supported by this helper")
    return raw


def pipe_open(path: str) -> int:
    handle = kernel32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
    if handle == INVALID_HANDLE_VALUE:
        err = ctypes.get_last_error()
        raise OSError(err, f"CreateFile {path} failed")
    mode = wintypes.DWORD(0)
    kernel32.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None)
    return handle


def pipe_write(handle: int, data: bytes) -> None:
    written = wintypes.DWORD(0)
    ok = kernel32.WriteFile(handle, data, len(data), ctypes.byref(written), None)
    if not ok:
        raise OSError(ctypes.get_last_error(), "WriteFile failed")


def pipe_read_some(handle: int, n: int) -> bytes:
    buf = ctypes.create_string_buffer(n)
    got = wintypes.DWORD(0)
    ok = kernel32.ReadFile(handle, buf, n, ctypes.byref(got), None)
    if not ok:
        raise OSError(ctypes.get_last_error(), "ReadFile failed")
    return buf.raw[: got.value]


def pipe_peek_avail(handle: int) -> int:
    avail = wintypes.DWORD(0)
    if not kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(avail), None):
        return 0
    return int(avail.value)


def send_console_command(container: str, command: str, wait_s: float = 4.0, expect: str = "") -> str:
    path = docker_pipe_path()
    handle = pipe_open(path)
    collected = bytearray()
    try:
        req = (
            f"POST /v1.43/containers/{container}/attach?stream=1&stdin=1&stdout=1&stderr=1 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: tcp\r\n"
            "Connection: Upgrade\r\n"
            "Content-Length: 0\r\n"
            "\r\n"
        ).encode("ascii")
        pipe_write(handle, req)

        header = bytearray()
        deadline = time.time() + 5
        while b"\r\n\r\n" not in header and time.time() < deadline:
            header.extend(pipe_read_some(handle, 256))
        head_text = header.decode("latin1", errors="replace")
        if "\r\n\r\n" not in head_text:
            raise RuntimeError("docker attach: no HTTP response")
        status_line = head_text.split("\r\n", 1)[0]
        if " 101 " not in status_line and " 200 " not in status_line:
            raise RuntimeError(f"docker attach failed: {status_line}")

        extra = header.split(b"\r\n\r\n", 1)[1]
        collected.extend(extra)
        line = command.strip() + "\n"
        pipe_write(handle, line.encode("ascii"))

        end = time.time() + wait_s
        while time.time() < end:
            avail = pipe_peek_avail(handle)
            if avail:
                collected.extend(pipe_read_some(handle, min(avail, 4096)))
                text = collected.decode("utf-8", errors="replace")
                if expect and expect in text:
                    break
            else:
                time.sleep(0.1)
        return collected.decode("utf-8", errors="replace")
    finally:
        kernel32.CloseHandle(handle)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--container", default="ac-worldserver")
    parser.add_argument("--command", default="saveall")
    parser.add_argument("--expect", default=None)
    parser.add_argument("--wait", type=float, default=4.0)
    args = parser.parse_args()
    expect = args.expect
    if expect is None:
        expect = "All players saved" if args.command.strip() == "saveall" else ""
    try:
        out = send_console_command(args.container, args.command, args.wait, expect)
    except Exception as exc:
        sys.stderr.write(f"FAIL: {exc}\n")
        return 1
    if expect and expect not in out:
        sys.stderr.write("FAIL: command sent but expected console text not seen\n")
        return 2
    sys.stdout.write("OK\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
