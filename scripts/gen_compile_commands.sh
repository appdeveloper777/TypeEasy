#!/usr/bin/env bash
# gen_compile_commands.sh — produce compile_commands.json for clangd / VS Code.
#
# Uses `bear` inside the same Docker image used by CI (gcc:13.2.0) so the
# include paths and defines match exactly what the engine is compiled with in
# production. The resulting file lives at build/compile_commands.json and is
# picked up by .clangd (CompileFlags) and by .vscode/c_cpp_properties.json
# (compileCommands).
#
# Run from the repo root:
#   bash scripts/gen_compile_commands.sh
#
# Requires Docker. Safe to re-run; overwrites build/compile_commands.json.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
mkdir -p "$ROOT/build"

echo "=== Generating compile_commands.json inside gcc:13.2.0 (this may take a few minutes) ==="

docker run --rm \
  -v "$ROOT":/app \
  -w /app/src \
  gcc:13.2.0 \
  bash -c '
    set -euo pipefail
    apt-get update -qq
    apt-get install -y --no-install-recommends \
      bear flex bison libssl-dev default-libmysqlclient-dev \
      libcurl4-openssl-dev libpq-dev freetds-dev libsqlite3-dev zlib1g-dev \
      >/dev/null
    make clean >/dev/null 2>&1 || true
    bear --output /app/build/compile_commands.json -- make -j2 typeeasy
  '

# bear writes paths relative to /app inside the container. Rewrite to the host
# repo root so VS Code on Windows resolves them correctly.
python3 - <<PY
import json, pathlib, sys
p = pathlib.Path("build/compile_commands.json")
data = json.loads(p.read_text(encoding="utf-8"))
host_root = r"${ROOT}"
for e in data:
    if "directory" in e:
        e["directory"] = e["directory"].replace("/app", host_root)
    if "file" in e:
        e["file"] = e["file"].replace("/app", host_root)
    if "command" in e:
        e["command"] = e["command"].replace("/app", host_root)
    if "arguments" in e:
        e["arguments"] = [a.replace("/app", host_root) for a in e["arguments"]]
p.write_text(json.dumps(data, indent=2), encoding="utf-8")
print(f"Rewrote {len(data)} entries -> {p}")
PY

echo "=== Done: build/compile_commands.json ($(wc -l < build/compile_commands.json) lines) ==="
echo "Restart the C/C++ extension (or clangd) in VS Code so it picks up the new index."
