#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHIM_DIR="$ROOT/tools/motor_nlu_shim"
OUT_DIR="$ROOT/src/native_libs"

echo "Building Rust shim inside container..."
docker run --rm -v "$SHIM_DIR":/src -w /src rust:1.72-bullseye sh -c "apt-get update >/dev/null && apt-get install -y pkg-config libssl-dev >/dev/null && cargo build --release"

mkdir -p "$OUT_DIR"
echo "Copying built library to $OUT_DIR"
cp "$SHIM_DIR/target/release/libmotor_nlu_shim.so" "$OUT_DIR/" || true

echo "Done. If the file wasn't copied, build locally with 'cargo build --release' and copy target/release/libmotor_nlu_shim.so to $OUT_DIR" 
