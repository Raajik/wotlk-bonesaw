#!/usr/bin/env python3
"""Post a patch-notes file to the Bonesaw Discord webhook.

Phase 6 of /bonesaw-ship used to read "Post to Discord" with nothing to run, so
it was done by hand or not at all -- 0.1.79 and 0.1.80 both shipped without the
notes ever reaching players. This is the missing half.

    python tools/post_patch_notes.py tools/patch-notes/0.1.80.md
    python tools/post_patch_notes.py tools/patch-notes/0.1.80.md --dry-run

The webhook URL lives in tools/client-update/discord.webhook, which is
gitignored -- it is a credential, do not commit it or echo it into a log.

Discord caps a message at 2000 characters. Long notes are split on blank lines
so a section never straddles two messages, and posted in order with a short
pause so Discord does not reorder or rate-limit them.

Exits non-zero on any failure so the ship stops rather than reporting success
it did not achieve.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEBHOOK_FILE = ROOT / "tools" / "client-update" / "discord.webhook"
LIMIT = 1900          # under Discord's 2000 so a code fence or mention cannot tip it over


def load_webhook() -> str:
    if not WEBHOOK_FILE.is_file():
        sys.exit(f"no webhook file at {WEBHOOK_FILE}")
    url = WEBHOOK_FILE.read_text(encoding="utf-8").strip()
    if not url.startswith("https://discord.com/api/webhooks/"):
        sys.exit("webhook file does not contain a discord webhook url")
    return url


def chunk(text: str) -> list:
    """Split on blank lines, never mid-section, never over the limit."""
    blocks = text.strip().split("\n\n")
    out, cur = [], ""
    for block in blocks:
        candidate = block if not cur else cur + "\n\n" + block
        if len(candidate) <= LIMIT:
            cur = candidate
            continue
        if cur:
            out.append(cur)
        # A single block over the limit still has to go somewhere: split it by
        # line rather than truncating and silently losing bullets.
        if len(block) > LIMIT:
            line_cur = ""
            for line in block.split("\n"):
                if len(line_cur) + len(line) + 1 > LIMIT:
                    out.append(line_cur)
                    line_cur = line
                else:
                    line_cur = line if not line_cur else line_cur + "\n" + line
            cur = line_cur
        else:
            cur = block
    if cur:
        out.append(cur)
    return out


def post(url: str, content: str) -> str:
    body = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8")
    req = urllib.request.Request(
        url + "?wait=true",
        data=body,
        headers={"Content-Type": "application/json", "User-Agent": "bonesaw-patch-notes/1.0"},
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

    parts = chunk(text)
    print(f"{path.name}: {len(text)} chars -> {len(parts)} message(s)")

    if args.dry_run:
        for i, part in enumerate(parts, 1):
            print(f"\n----- message {i}/{len(parts)} ({len(part)} chars) -----")
            print(part)
        return 0

    url = load_webhook()
    for i, part in enumerate(parts, 1):
        try:
            msg_id = post(url, part)
        except urllib.error.HTTPError as err:
            detail = err.read().decode("utf-8", "replace")[:300]
            sys.exit(f"message {i}/{len(parts)} rejected ({err.code}): {detail}")
        except urllib.error.URLError as err:
            sys.exit(f"message {i}/{len(parts)} failed to send: {err.reason}")
        print(f"  posted {i}/{len(parts)}  id {msg_id}")
        if i < len(parts):
            time.sleep(1.0)

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
