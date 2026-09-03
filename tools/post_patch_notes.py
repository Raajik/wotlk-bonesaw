#!/usr/bin/env python3
"""Post a patch-notes file to the Bonesaw Discord webhook.

Phase 6 of /bonesaw-ship used to read "Post to Discord" with nothing to run, so
it was done by hand or not at all -- 0.1.79 and 0.1.80 both shipped without the
notes ever reaching players. This is the missing half.

    python tools/post_patch_notes.py tools/patch-notes/0.1.80.md
    python tools/post_patch_notes.py tools/patch-notes/0.1.80.md --dry-run

The webhook URL lives in tools/client-update/discord.webhook, which is
gitignored -- it is a credential, do not commit it or echo it into a log.

The Discord post is attachment-only: the message body is the notes' title line
plus a pointer, and the full notes ride along as a .txt attachment. Notes of
any length post as ONE message - the 2000-character limit never splits or
truncates anything.

Exits non-zero on any failure so the ship stops rather than reporting success
it did not achieve.
"""

import argparse
import io
import json
import re
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEBHOOK_FILE = ROOT / "tools" / "client-update" / "discord.webhook"
UA = "bonesaw-patch-notes/1.2"


def load_webhook() -> str:
    if not WEBHOOK_FILE.is_file():
        sys.exit(f"no webhook file at {WEBHOOK_FILE}")
    url = WEBHOOK_FILE.read_text(encoding="utf-8").strip()
    if not url.startswith("https://discord.com/api/webhooks/"):
        sys.exit("webhook file does not contain a discord webhook url")
    return url


def post(url: str, content: str, filename: str | None = None,
         attachment: bytes | None = None) -> str:
    """Post one message; with an attachment, use a multipart upload so the
    full notes ride along as a .txt file regardless of length."""
    if attachment is None:
        body = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8")
        req = urllib.request.Request(
            url + "?wait=true",
            data=body,
            headers={"Content-Type": "application/json", "User-Agent": UA},
            method="POST",
        )
    else:
        boundary = "----bonesawBoundary" + uuid.uuid4().hex
        buf = io.BytesIO()
        buf.write(f"--{boundary}\r\n".encode())
        buf.write(b"Content-Disposition: form-data; name=\"payload_json\"\r\n\r\n")
        buf.write(json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8"))
        buf.write(f"\r\n--{boundary}\r\n".encode())
        buf.write(
            f"Content-Disposition: form-data; name=\"files[0]\"; "
            f"filename=\"{filename}\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n".encode()
        )
        buf.write(attachment)
        buf.write(f"\r\n--{boundary}--\r\n".encode())
        req = urllib.request.Request(
            url + "?wait=true",
            data=buf.getvalue(),
            headers={"Content-Type": f"multipart/form-data; boundary={boundary}", "User-Agent": UA},
            method="POST",
        )
    with urllib.request.urlopen(req, timeout=30) as resp:
        payload = json.loads(resp.read().decode("utf-8"))
        return payload.get("id", "?")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("notes", help="path to the patch notes file")
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would be posted, contact nobody")
    args = ap.parse_args()

    path = Path(args.notes)
    if not path.is_file():
        sys.exit(f"no such notes file: {path}")
    text = path.read_text(encoding="utf-8")

    # Client-facing strings are ASCII only; a smart quote pasted from a doc is
    # the usual way that rule gets broken.
    bad = sorted({c for c in text if ord(c) > 127})
    if bad:
        sys.exit(f"notes contain non-ascii characters: {bad!r}")

    # Advisory lines (e.g. retest_list's char count) must never reach players;
    # they used to leak into the notes file and then into the Discord post.
    text = "\n".join(line for line in text.splitlines()
                     if not re.match(r"^\[\d+ chars;", line)).strip() + "\n"

    # Attachment-only: the body is just the title line plus a pointer, and the
    # full notes ride as a .txt attachment - no chunking, no truncation, no
    # 2000-character ceiling to think about.
    title = text.splitlines()[0].strip() if text.splitlines() else "Bonesaw patch notes"
    body = title + "\nFull patch notes attached - open the file to read them."
    attach_name = f"Bonesaw-{path.stem}-patch-notes.txt"
    print(f"{path.name}: {len(text)} chars in attachment; body {len(body)} chars; attached as {attach_name}")

    if args.dry_run:
        print("\n----- body -----")
        print(body)
        print(f"\n----- attachment: {attach_name} ({len(text.encode('utf-8'))} bytes) -----")
        return 0

    url = load_webhook()
    try:
        msg_id = post(url, body, attach_name, text.encode("utf-8"))
    except urllib.error.HTTPError as err:
        detail = err.read().decode("utf-8", "replace")[:300]
        sys.exit(f"post rejected ({err.code}): {detail}")
    except urllib.error.URLError as err:
        sys.exit(f"post failed to send: {err.reason}")
    print(f"  posted  id {msg_id}  (notes attached as {attach_name})")
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
