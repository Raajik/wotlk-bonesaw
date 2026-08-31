@echo off
docker run --rm -v A:/wow-bonesaw:/repo -w /repo/tools/launcher rust:latest bash -c "rustup target add x86_64-pc-windows-gnu && apt-get update -qq && apt-get install -y -qq mingw-w64 && cargo build --release --target x86_64-pc-windows-gnu" > A:\wow-bonesaw\tools\_build_launcher_docker.log 2>&1
