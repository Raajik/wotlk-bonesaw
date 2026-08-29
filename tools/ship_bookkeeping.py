#!/usr/bin/env python3
"""Automate the post-replace half of a Bonesaw ship (Phases 4-6 bookkeeping).

Everything after "the new worldserver is running" used to be a dozen hand-run
commands -- version bump, launcher build, release create, manifest upload,
patch notes, Discord post, release edit, tag, push. Each hand-run step was a
forgotten-step risk; the manifest went stale twice (0.1.105/0.1.106 era) for
exactly that reason. This script does the whole tail in one pass, in the
right order, and refuses to continue on any failed step.

    python tools/ship_bookkeeping.py --version 0.1.107 [--no-client] [--dry-run]

Order of operations (tag-last ordering: the ship tag is the FINISH line, so
`git log ship/<latest>..HEAD` is empty when this exits):
  1. bump tools/client-update/Bonesaw.version (exactly +0.0.1)   [server-only skips client steps]
  2. build client patch MPQs (build_patch.py)                    [--no-client skips]
  3. build Bonesaw.exe + manifest (build_launcher.py)            [--no-client skips]
  4. gh release create vX.Y.Z with the launcher exe              [--no-client skips]
  5. build_launcher.py --verify                                  [--no-client skips]
  6. commit manifest + version ("Manifest X.Y.Z")
  7. upload manifest to the 'updater' release (--clobber)
  8. verify the live manifest serves X.Y.Z (curl -sfL, cache-busted)
  9. write tools/patch-notes/X.Y.Z.md (commits since last ship/* + retest list)
 10. post_patch_notes.py (Discord)
 11. gh release edit vX.Y.Z --notes-file
 12. commit patch notes ("Patch notes X.Y.Z")
 13. tag ship/X.Y.Z  (LAST, after every artifact commit)
 14. push Playerbot + tag

Requires: gh authenticated, docker NOT required, network for github.
"""
from __future__ import annotations

import argparse
import datetime
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "tools" / "client-update" / "Bonesaw.version"
MANIFEST = ROOT / "tools" / "client-update" / "Bonesaw.manifest.txt"
NOTES_DIR = ROOT / "tools" / "patch-notes"
LAUNCHER_EXE = ROOT / "tools" / "launcher" / "dist" / "Bonesaw.exe"
REPO = "Raajik/wotlk-bonesaw"
MANIFEST_URL = ("https://github.com/Raajik/wotlk-bonesaw/releases/download/"
                "updater/Bonesaw.manifest.txt")


def run(cmd: str, *, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    print(f"  $ {cmd}")
    if capture:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=ROOT)
    else:
        r = subprocess.run(cmd, shell=True, cwd=ROOT)
    if check and r.returncode != 0:
        sys.exit(f"FAILED (exit {r.returncode}): {cmd}")
    return r


def sh_out(cmd: str) -> str:
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=ROOT)
    return (r.stdout or "").strip()


def git(*args: str) -> str:
    return sh_out("git " + " ".join(f'"{a}"' if " " in a else a for a in args))


def bump_version(current: str) -> str:
    a, b, c = (int(p) for p in current.split("."))
    return f"{a}.{b}.{c + 1}"


def latest_ship_tag() -> str:
    tag = sh_out('git describe --tags --match "ship/*" --abbrev=0')
    return tag or ""


def unshipped_commits() -> list[str]:
    tag = latest_ship_tag()
    rng = f"{tag}..HEAD" if tag else "HEAD~15..HEAD"
    out = sh_out(f'git log --oneline "{rng}"')
    return [l for l in out.splitlines() if l.strip()]


def client_paths_changed() -> bool:
    tag = latest_ship_tag()
    rng = f"{tag}..HEAD" if tag else "HEAD"
    out = sh_out(
        f'git diff --stat "{rng}" -- modules/mod-living-gear/client_addon '
        f"tools/client-patch tools/launcher")
    return bool(out.strip())


def retest_list() -> str:
    r = run("python tools/retest_list.py", check=False, capture=True)
    return (r.stdout or "").strip()


def write_patch_notes(version: str) -> Path:
    commits = unshipped_commits()
    bullets = []
    for line in commits:
        # subject after the hash; skip pure bookkeeping subjects
        subject = line.split(" ", 1)[1] if " " in line else line
        low = subject.lower()
        if low.startswith(("bump bonesaw.version", "manifest ")):
            continue
        bullets.append(f"- {subject}")
    if not bullets:
        bullets = ["- Server stability and bookkeeping updates."]

    lines = [f"**Bonesaw {version} - patch notes**", ""]
    lines.append("**Changes**")
    lines += bullets
    lines.append("")
    retest = retest_list()
    if retest:
        lines.append(retest)
    lines.append("Close Wow and run Bonesaw.exe to update.")

    NOTES_DIR.mkdir(exist_ok=True)
    path = NOTES_DIR / f"{version}.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  wrote {path}")
    return path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True,
                    help="the version being shipped, e.g. 0.1.107")
    ap.add_argument("--no-client", action="store_true",
                    help="no client files changed: skip MPQ/launcher/release steps")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the plan without executing mutating steps")
    args = ap.parse_args()

    version = args.version
    current = VERSION_FILE.read_text().strip()
    if bump_version(current) != version:
        sys.exit(f"version mismatch: {VERSION_FILE} says {current}; "
                 f"expected the next bump {bump_version(current)}, got {version}")
    tag = f"ship/{version}"
    if tag in git("tag", "--list"):
        sys.exit(f"{tag} already exists")
    dirty = [l for l in git("status", "--porcelain").splitlines()
             if l and not l.startswith("??")]
    if dirty:
        sys.exit("tracked files uncommitted; commit first:\n" + "\n".join(dirty))

    client = not args.no_client
    print(f"Shipping {version} (client={'yes' if client else 'no'})")
    if args.dry_run:
        print("dry-run: no mutating steps executed")
        return

    # 1. version bump
    VERSION_FILE.write_text(version + "\n")
    run(f'git add "{VERSION_FILE.relative_to(ROOT)}" && '
        f'git commit -m "Bump Bonesaw.version to {version}"')

    if client:
        # 2-5. MPQs, launcher, release, verify
        run("python tools/client-patch/build_patch.py")
        run("python tools/launcher/build_launcher.py")
        run(f'gh release create v{version} --repo {REPO} --latest '
            f'--title "Bonesaw client {version}" --notes "Run Bonesaw.exe." '
            f'"{LAUNCHER_EXE}"')
        run("python tools/launcher/build_launcher.py --verify")

        # 6. manifest commit
        run(f'git add "{MANIFEST.relative_to(ROOT)}" && '
            f'git commit -m "Manifest {version}"')

        # 7-8. publish + verify the live manifest
        run(f"gh release upload updater \"{MANIFEST}\" --repo {REPO} --clobber")
        chk = run(f'curl -sfL "{MANIFEST_URL}?v={version}"', capture=True)
        head = (chk.stdout or "").splitlines()
        served = next((l.split()[1] for l in head if l.startswith("version ")), "?")
        if served != version:
            sys.exit(f"live manifest serves {served}, not {version} -- "
                     "upload did not take effect; re-run step 7 before continuing")
        print(f"  live manifest serves {version} -- OK")

    # 9-12. patch notes
    notes = write_patch_notes(version)
    run(f"python tools/post_patch_notes.py \"{notes}\" --dry-run")
    run(f'python tools/post_patch_notes.py "{notes}"')
    if client:
        run(f"gh release edit v{version} --repo {REPO} --notes-file \"{notes}\"")
    else:
        # No client ship -> no GitHub release exists (created only in step 4).
        # Create a server-only release so the notes still live on the tag page.
        run(f'gh release create v{version} --repo {REPO} --latest '
            f'--title "Bonesaw {version}" --notes-file "{notes}"')
    run(f'git add "{notes.relative_to(ROOT)}" && '
        f'git commit -m "Patch notes {version}"')

    # 13-14. tag LAST, then push everything together
    run(f'git tag "{tag}"')
    run(f"git push origin Playerbot \"{tag}\"")

    print()
    print(f"Ship {version} bookkeeping complete. Tag {tag} is the finish line; "
          "git log ship/..HEAD should be empty.")


if __name__ == "__main__":
    main()
