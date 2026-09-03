import os
pf = 'tools/launcher/build_launcher.py'
s = open(pf, 'r', encoding='utf-8', newline='').read()
old = """    print("building Bonesaw.exe ...")
    subprocess.run(["cargo", "build", "--release"], cwd=HERE, check=True)
""".replace('\n', '\r\n')
new = """    print("building Bonesaw.exe ...")
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
""".replace('\n', '\r\n')
n = s.count(old)
assert n == 1, f'anchor: {n}'
s = s.replace(old, new)
old2 = "    shutil.copy2(BUILT, DIST)\r\n"
new2 = "    shutil.copy2(built, DIST)\r\n"
n2 = s.count(old2)
assert n2 == 1, f'copy anchor: {n2}'
s = s.replace(old2, new2)
open(pf, 'w', encoding='utf-8', newline='').write(s)
print('patched')
