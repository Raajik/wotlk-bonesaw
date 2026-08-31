"""
Build Bonesaw.exe and write the manifest that tells launchers about it.

Run after tools/client-patch/build_patch.py, from anywhere:

    python tools/launcher/build_launcher.py

It copies the freshly built MPQs into the launcher payload, builds the release
exe, and rewrites tools/client-update/Bonesaw.manifest.txt with the new version,
size, and SHA256. The realmlist line already in the manifest is carried over.
"""
from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DIST_MPQ = ROOT / "tools" / "client-patch" / "dist"
PAYLOAD = HERE / "payload"
UPDATE = ROOT / "tools" / "client-update"
MANIFEST = UPDATE / "Bonesaw.manifest.txt"
BUILT = HERE / "target" / "release" / "Bonesaw.exe"
DIST = HERE / "dist" / "Bonesaw.exe"
RELEASE_URL = "https://github.com/Raajik/wotlk-bonesaw/releases/download/v{version}/Bonesaw.exe"

# enUS and enGB are byte-identical, so only one copy goes into the exe.
PAYLOAD_FILES = ["patch-Y.MPQ", "patch-enUS-4.MPQ"]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_realmlist() -> str | None:
    if not MANIFEST.exists():
        return None
    for line in MANIFEST.read_text().splitlines():
        if line.startswith("realmlist "):
            return line.split(None, 1)[1].strip()
    return None


def verify() -> None:
    """Confirm the manifest still describes dist/Bonesaw.exe."""
    if not DIST.exists():
        raise SystemExit(f"{DIST} does not exist. Build first.")
    if not MANIFEST.exists():
        raise SystemExit(f"{MANIFEST} does not exist. Build first.")
    want = None
    for line in MANIFEST.read_text().splitlines():
        if line.startswith("file "):
            _, sha, size, _name = line.split()
            want = (sha, int(size))
    if want is None:
        raise SystemExit("manifest has no file line")
    got = (sha256(DIST), DIST.stat().st_size)
    if got != want:
        raise SystemExit(
            "MISMATCH\n"
            f"  manifest: {want[0]} {want[1]}\n"
            f"  dist:     {got[0]} {got[1]}\n"
            "Re-run without --verify, then re-upload and re-push together."
        )
    print(f"manifest matches {DIST}")
    print(f"  {got[0]} {got[1]} bytes")


def main() -> None:
    if "--verify" in sys.argv[1:]:
        return verify()
    version = (UPDATE / "Bonesaw.version").read_text().strip()
    if not all(p.isdigit() for p in version.split(".")):
        raise SystemExit(f"Bonesaw.version is not numeric: {version!r}")

    gb = DIST_MPQ / "patch-enGB-4.MPQ"
    us = DIST_MPQ / "patch-enUS-4.MPQ"
    if gb.exists() and us.exists() and gb.read_bytes() != us.read_bytes():
        raise SystemExit(
            "patch-enGB-4.MPQ and patch-enUS-4.MPQ differ. The launcher payload "
            "carries one copy for both locales; teach payload.rs about the split "
            "before shipping this."
        )

    PAYLOAD.mkdir(exist_ok=True)
    for name in PAYLOAD_FILES:
        src = DIST_MPQ / name
        if not src.exists():
            raise SystemExit(f"missing {src}. Run tools/client-patch/build_patch.py first.")
        dst = PAYLOAD / name
        # Only copy when the bytes actually differ. Touching the payload forces a
        # rebuild, and the exe is not byte-reproducible, so a needless rebuild
        # would invalidate a hash that may already be published.
        if dst.exists() and dst.read_bytes() == src.read_bytes():
            print(f"payload  {name}  unchanged")
        else:
            shutil.copy2(src, dst)
            print(f"payload  {name}  {src.stat().st_size:,} bytes")

    print("building Bonesaw.exe ...")
    # The msvc toolchain needs link.exe, which left this machine with Visual
    # Studio; the container cross-build (tools/_launcher_build.cmd, mingw-w64
    # inside rust:latest) produces the same Windows exe without it.
    try:
        subprocess.run(["cargo", "build", "--release"], cwd=HERE, check=True)
        built = BUILT
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f"msvc build unavailable ({exc.__class__.__name__}); cross-building in a container ...")
        subprocess.run([str(ROOT / "tools" / "_launcher_build.cmd")], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        built = HERE / "target" / "x86_64-pc-windows-gnu" / "release" / "Bonesaw.exe"
        if not built.exists():
            raise SystemExit("cross-build produced no exe")

    # The release artifact is a stable copy, so re-running this script cannot
    # quietly replace the file whose hash went into the manifest.
    DIST.parent.mkdir(exist_ok=True)
    shutil.copy2(built, DIST)

    size = DIST.stat().st_size
    digest = sha256(DIST)
    realmlist = read_realmlist()

    lines = ["BONESAW 1", f"version {version}"]
    if realmlist:
        lines.append(f"realmlist {realmlist}")
    lines += [
        f"file {digest} {size} Bonesaw.exe",
        f"url {RELEASE_URL.format(version=version)}",
    ]
    MANIFEST.write_text("\n".join(lines) + "\n")

    print()
    print(f"{DIST}  {size:,} bytes")
    print(f"sha256 {digest}")
    print(f"wrote {MANIFEST}")
    if not realmlist:
        print(
            "\nNOTE: no realmlist line in the manifest. Add 'realmlist <host>' to\n"
            f"{MANIFEST} if you want the launcher to set realmlist.wtf for players."
        )
    print(
        f"\nNext:\n"
        f"  gh release create v{version} --repo Raajik/wotlk-bonesaw --latest "
        f'--title "Bonesaw client {version}" --notes "Run Bonesaw.exe." "{DIST}"\n'
        f"  then commit and push {MANIFEST.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    sys.exit(main())
