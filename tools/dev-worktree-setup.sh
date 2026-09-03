#!/usr/bin/env bash
# Orca worktree setup hook (see orca.yaml -> scripts.setup).
# Runs inside a freshly created git worktree, so it must be fast and
# non-destructive: everything here is per-checkout state only.

set -u

cd "$(dirname "$0")/.." || exit 1
echo "[bonesaw-setup] workspace: ${ORCA_WORKTREE_PATH:-$PWD}"

# --- conf/*.conf -----------------------------------------------------------
# Runtime configs are gitignored. Copy the dist templates on first run so the
# worldserver/authserver can actually boot from this checkout. Never overwrite
# an existing conf file -- worktrees may share tuned settings.
mkdir -p conf
for tmpl in conf/dist/*.conf.dist; do
    [ -e "$tmpl" ] || continue
    target="conf/$(basename "$tmpl" .dist)"
    if [ ! -e "$target" ]; then
        cp "$tmpl" "$target"
        echo "[bonesaw-setup] created $target"
    fi
done

# --- build dir stub --------------------------------------------------------
# Out-of-source builds are required (in-source is blocked). Each worktree gets
# its own build dir; actual cmake configure is too slow for the 2-minute hook
# timeout, so just leave a marker explaining how to do it.
if [ ! -d build ]; then
    mkdir build
    cat >build/HOW-TO-BUILD.txt <<'EOF'
Out-of-source build (required):
  cd build
  cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSCRIPTS=static -DMODULES=static
  cmake --build . -j
EOF
    echo "[bonesaw-setup] created build/ stub"
fi

echo "[bonesaw-setup] done"
exit 0
