#!/usr/bin/env bash
# Regresión: handshake de ABI host↔plugin en load_native (0.1.8 re-tag #4).
#
# Un plugin compilado contra otra disposición de TEHostAPI se registraba sin
# error y después TODAS sus builtins devolvían []/0 en silencio (JunX 2026-09-20:
# libte_sqlite.so del paquete 0.0.13 bajo un host 0.1.6 → /api/register y
# /api/ranking devolvían [] con la BD intacta). Ahora el host exige
# te_module_abi_version()/te_module_api_size() y rechaza RUIDOSAMENTE:
#   1. plugin legacy sin handshake        → load_native()=0 + "REJECTED" en stderr
#   2. ídem con TYPEEASY_ALLOW_LEGACY_PLUGINS=1 → carga con WARNING y funciona
#   3. plugin con versión/tamaño distinto  → REJECTED ... ABI mismatch
#   4. plugin correcto                     → load_native()=1 y la builtin responde
# Uso: bash tests/regress/run_plugin_abi_handshake.sh <typeeasy-bin>
set -u
BIN="${1:-./src/typeeasy}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fails=0

stub_body='
#include "te_builtins.h"
static TEHostAPI H;
static int stub_ping(ASTNode *node, ASTNode *args) { (void)node; (void)args; H.set_ret_int(7); return 1; }
void te_module_register(const TEHostAPI *host) { H = *host; host->register_builtin("stub_ping", stub_ping); }
'
printf '%s\n' "$stub_body" > "$W/legacy.c"
printf '%s\nTE_PLUGIN_EXPORT_ABI()\n' "$stub_body" > "$W/ok.c"
printf '%s\nint te_module_abi_version(void){ return 99; }\nint te_module_api_size(void){ return (int)sizeof(TEHostAPI); }\n' "$stub_body" > "$W/bad.c"
for n in legacy ok bad; do
  gcc -shared -fPIC -O0 -I"$SRC" "$W/$n.c" -o "$W/libte_$n.so" || { echo "FAIL: gcc stub $n"; exit 1; }
done
cat > "$W/t.te" <<'EOF'
let rc = load_native(PLUGIN);
println(concat("rc=", ("" + rc)));
if (rc == 1) { println(concat("ping=", ("" + stub_ping()))); }
EOF

run() { # $1=plugin path  $2..=env
  local p="$1"; shift
  sed "s#PLUGIN#\"$p\"#" "$W/t.te" > "$W/run.te"
  ( cd "$W" && env "$@" "$BIN" run.te > "$W/out.txt" 2> "$W/err.txt" )
  OUT="$(cat "$W/out.txt")"; ERR="$(cat "$W/err.txt")"
}

run "$W/libte_legacy.so" X=1
echo "1) legacy: out=[$(echo "$OUT" | tr '\n' ' ')] rejected=$(echo "$ERR" | grep -c REJECTED)"
echo "$OUT" | grep -q "rc=0" && echo "$ERR" | grep -q "REJECTED.*no ABI handshake" || { echo "FAIL: legacy plugin was not rejected"; fails=$((fails+1)); }

run "$W/libte_legacy.so" TYPEEASY_ALLOW_LEGACY_PLUGINS=1
echo "2) legacy+allow: out=[$(echo "$OUT" | tr '\n' ' ')] warning=$(echo "$ERR" | grep -c 'WARNING.*legacy plugin')"
echo "$OUT" | grep -q "rc=1" && echo "$OUT" | grep -q "ping=7" && echo "$ERR" | grep -q "WARNING.*legacy" || { echo "FAIL: escape hatch did not load the legacy plugin"; fails=$((fails+1)); }

run "$W/libte_bad.so" X=1
echo "3) mismatch: out=[$(echo "$OUT" | tr '\n' ' ')] rejected=$(echo "$ERR" | grep -c 'REJECTED.*ABI mismatch')"
echo "$OUT" | grep -q "rc=0" && echo "$ERR" | grep -q "ABI mismatch.*v99" || { echo "FAIL: mismatched plugin was not rejected"; fails=$((fails+1)); }

run "$W/libte_ok.so" X=1
echo "4) ok: out=[$(echo "$OUT" | tr '\n' ' ')]"
echo "$OUT" | grep -q "rc=1" && echo "$OUT" | grep -q "ping=7" && ! echo "$ERR" | grep -q "REJECTED\|WARNING" || { echo "FAIL: matching plugin did not load cleanly: $ERR"; fails=$((fails+1)); }

echo "PLUGIN_ABI_RESULT: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
[ $fails = 0 ]
