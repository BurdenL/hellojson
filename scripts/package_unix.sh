#!/usr/bin/env bash
# Native builds only. Linux archives use the target system's Qt runtime.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
platform="$(uname -s)"
case "$platform" in Darwin|Linux) ;; *) echo 'Run this script on macOS or Linux.' >&2; exit 1;; esac
build="${BUILD_DIRECTORY:-$root/out/build/$platform-release}"
dist="$root/out/dist"
mkdir -p "$dist"
stage="$(mktemp -d "$dist/$platform-stage.XXXXXX")"
args=(-S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON)
if [[ -n "${QT_ROOT:-}" ]]; then args+=("-DCMAKE_PREFIX_PATH=$QT_ROOT"); fi
cmake "${args[@]}"
cmake --build "$build" --config Release --parallel
ctest --test-dir "$build" -C Release --output-on-failure
cmake --install "$build" --config Release --prefix "$stage"
cp "$root/LICENSE" "$root/NOTICE" "$root/docs/CROSS_PLATFORM.md" "$stage/"
if [[ "$platform" == Darwin ]]; then
    deploy="${QT_ROOT:+$QT_ROOT/bin/}macdeployqt"
    "$deploy" "$stage/hellojson.app" -always-overwrite
    cp "$root/LICENSE" "$root/NOTICE" "$stage/hellojson.app/Contents/Resources/"
    "$stage/hellojson.app/Contents/MacOS/hellojson" --smoke-test
    # Signing and notarization require the publisher's Apple credentials.
    ditto -c -k --sequesterRsrc --keepParent "$stage/hellojson.app" \
        "$dist/HelloJson-macOS-$(uname -m).zip"
else
    QT_QPA_PLATFORM=offscreen "$stage/bin/hellojson" --smoke-test
    tar -czf "$dist/HelloJson-Linux-$(uname -m).tar.gz" -C "$stage" .
fi
echo "Package created in $dist; staging files retained at $stage"
