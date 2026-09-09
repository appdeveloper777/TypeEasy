#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# audit_core_debt.sh — Guard para que la deuda del núcleo (src/ast.c) no crezca.
#
# Qué vigila (Fase 0 del plan de pago de deuda técnica):
#   1. GLOBALES: ninguna variable global nueva `g_*` a nivel de archivo en los
#      módulos del intérprete (excepto `g_vm`, el contenedor de estado de la Fase 3:
#      el estado NUEVO va como campo de TeVM en te_vm.h, no como global suelto).
#      módulos del intérprete. El estado global es la causa raíz de los bugs de
#      "datos que sobreviven entre requests" (#38, 30c) y de que el intérprete no
#      sea reentrante. Las que ya existen están en la baseline y solo pueden
#      desaparecer.
#   2. FUNCIONES LARGAS: ninguna función nueva > MAX_FN_LINES líneas, y las que ya
#      superan el límite (baseline) no pueden crecer. Nuevas features se escriben
#      en funciones nuevas, no dentro de interpret_call_method_impl.
#
# Baseline: scripts/core_debt_baseline.txt (generada con --update-baseline).
# Regenerarla SOLO cuando la deuda baja (una función se partió, un global se
# eliminó). Si la regenerás para "hacer pasar" un aumento, el guard no sirve.
#
# Uso:
#   bash scripts/audit_core_debt.sh                  # exit 0 = ok, 1 = deuda creció
#   bash scripts/audit_core_debt.sh --update-baseline
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BASELINE="scripts/core_debt_baseline.txt"
MAX_FN_LINES=300
# Se audita TODO el intérprete (src/*.c) salvo lo generado por flex/bison. Antes (0.1.0) solo
# se vigilaban ast.c y los módulos partidos; te_linq_ops.c/te_csv.c/te_bytecode.c crecían sin guardia.
FILES=()
for f in src/*.c; do
  case "$f" in src/lex.yy.c|src/parser.tab.c) continue;; esac
  FILES+=("$f")
done

# Globales g_* definidos a nivel de archivo (columna 0, con o sin static; no extern).
scan_globals() {
  for f in "${FILES[@]}"; do
    [ -f "$f" ] || continue
    # En dos pasos para evitar backtracking exponencial (clases solapadas con [:space:]/\*
    # colgaban grep en te_vm.c). 1) línea de declaración en columna 0; 2) token g_* seguido
    # de `=`/`;`; 3) sin `(` antes del token (prototipos/llamadas) ni extern.
    grep -nE '^(static[[:space:]]+)?(const[[:space:]]+)?[A-Za-z_][A-Za-z0-9_]*[[:space:]\*]' "$f" \
      | grep -E '[[:space:]\*]g_[A-Za-z0-9_]+[[:space:]]*(\[[^]]*\])*[[:space:]]*(=|;)' \
      | grep -vE '^[0-9]+:[^(]*\([^;]*[[:space:]\*]g_[A-Za-z0-9_]+[[:space:]]*(\[[^]]*\])*[[:space:]]*(=|;)' \
      | grep -vE '^[0-9]+:(static[[:space:]]+)?extern' \
      | grep -vE '\bg_vm\b' \
      | sed -E 's/^[0-9]+://; s/.*[[:space:]\*](g_[A-Za-z0-9_]+)[[:space:]]*(\[[^]]*\])*[[:space:]]*(=|;).*/\1/' \
      | sed "s|^|global $f |"
  done | sort -u
}

# Funciones: cabecera en columna 0 que termina en ")" + "{" y cierre "}" en columna 0.
scan_long_functions() {
  for f in "${FILES[@]}"; do
    [ -f "$f" ] || continue
    awk -v file="$f" -v max="$MAX_FN_LINES" '
      # Cabecera de función en columna 0; se excluyen sentencias de control mal indentadas.
      /^[A-Za-z_][^;#]*\)[[:space:]]*\{[[:space:]]*$/ && $0 !~ /^(if|for|while|switch|else|do|return|case)[[:space:](]/ { hdr=$0; start=NR; next }
      /^\}[[:space:]]*$/ && start {
        len=NR-start
        if (len > max) {
          name=hdr; sub(/\(.*/, "", name); sub(/.*[[:space:]\*]/, "", name)
          printf "fn %s %s %d\n", file, name, len
        }
        start=0
      }' "$f"
  done | sort
}

current="$( { scan_globals; scan_long_functions; } )"

if [ "${1:-}" = "--update-baseline" ]; then
  {
    echo "# core_debt_baseline — generada por scripts/audit_core_debt.sh --update-baseline"
    echo "# Formato: 'global <archivo> <nombre>' | 'fn <archivo> <nombre> <lineas>'. Solo puede achicarse."
    echo "$current"
  } > "$BASELINE"
  echo "audit-core-debt: baseline actualizada ($(grep -c '^global' "$BASELINE") globales, $(grep -c '^fn' "$BASELINE") funciones > ${MAX_FN_LINES})."
  exit 0
fi

[ -f "$BASELINE" ] || { echo "audit-core-debt: falta $BASELINE (correr --update-baseline)" >&2; exit 1; }

fail=0
# Comparación en un solo awk (sin un grep por línea: en MSYS cada fork cuesta ~50 ms y con
# 165 globales el script tardaba casi 3 minutos).
report="$(awk -v max="$MAX_FN_LINES" '
  FNR==NR { if ($1=="global") base_g[$2" "$3]=1; else if ($1=="fn") base_f[$2" "$3]=$4; next }
  $1=="global" { k=$2" "$3; cur_g[k]=1; if (!(k in base_g)) print "NUEVO GLOBAL: " k }
  $1=="fn"     { k=$2" "$3; cur_f[k]=1
                 if (!(k in base_f)) print "FUNCION NUEVA > " max " LINEAS: " k " (" $4 ")"
                 else if ($4+0 > base_f[k]+0) print "FUNCION CRECIO: " k " (" base_f[k] " -> " $4 " lineas)" }
  END {
    for (k in base_g) if (!(k in cur_g)) gone = gone "  global " k "\n"
    for (k in base_f) if (!(k in cur_f)) gone = gone "  fn " k " " base_f[k] "\n"
    if (gone != "") printf "GONE\n%s", gone
  }' "$BASELINE" <(echo "$current"))"
fail=$(echo "$report" | grep -cE '^(NUEVO GLOBAL|FUNCION)' || true)
echo "$report" | grep -E '^(NUEVO GLOBAL|FUNCION)' || true
gone="$(echo "$report" | sed -n '/^GONE$/,$p' | sed '1d')"
[ -n "$gone" ] && { echo "audit-core-debt: deuda saldada (podés regenerar la baseline):"; echo "$gone"; }

n_g=$(echo "$current" | grep -c '^global' || true); n_f=$(echo "$current" | grep -c '^fn' || true)
echo "audit-core-debt: ${n_g} globales, ${n_f} funciones > ${MAX_FN_LINES} lineas."
if [ "$fail" -gt 0 ]; then
  cat >&2 <<EOF

FAIL: ${fail} incremento(s) de deuda en el núcleo.
- Global nuevo: guardá el estado en el contexto del request/VM (te_reqstate, TeFrame) o
  pasalo como parámetro; no en g_*.
- Función > ${MAX_FN_LINES} líneas: extraé la lógica nueva a una función propia (static)
  en vez de agregar ramas a la existente.
Si de verdad bajó la deuda (partiste una función, borraste un global), regenerá con
  bash scripts/audit_core_debt.sh --update-baseline
EOF
  exit 1
fi
echo "OK: la deuda del núcleo no creció."
