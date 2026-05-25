#!/usr/bin/env bash
# ============================================================================
# install.sh — Instala TypeEasy en un Linux con systemd
#
# Copia:
#   - Binario C  → /usr/bin/typeeasy-server
#   - Wrapper    → /usr/bin/typeeasy
#   - Templates  → /usr/share/typeeasy/templates/
#   - Systemd    → /etc/systemd/system/typeeasy-api@.service
#   - Defaults   → /etc/default/typeeasy-api  (si no existe)
#
# Crea:
#   - User/group `typeeasy` (system, no login)
#   - /opt/typeeasy/apis  (directorio default de endpoints)
#
# Uso:
#   sudo ./installer/linux/install.sh
#   sudo ./installer/linux/install.sh --binary /path/to/typeeasy_api
#
# Despues de instalar:
#   sudo cp tus_endpoints/*.te /opt/typeeasy/apis/
#   sudo chown -R typeeasy:typeeasy /opt/typeeasy
#   sudo systemctl enable --now typeeasy-api@8080
#   curl http://127.0.0.1:8080/api/health
# ============================================================================
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Run with sudo." >&2; exit 1; }

# --- Locate source tree (this script lives in installer/linux/) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# --- Parse args ---
BINARY=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

# --- Locate binary ---
if [[ -z "$BINARY" ]]; then
  for cand in \
    "$REPO_DIR/bin/typeeasy_api" \
    "$REPO_DIR/typeeasy_api" \
    "$REPO_DIR/api_server/typeeasy_api"; do
    [[ -x "$cand" ]] && BINARY="$cand" && break
  done
fi
[[ -x "$BINARY" ]] || { echo "No se encontro binario. Pasalo con --binary <path>"; exit 1; }

echo "[install] Binary:    $BINARY"
echo "[install] Repo dir:  $REPO_DIR"

# --- Create user/group ---
if ! id typeeasy &>/dev/null; then
  echo "[install] Creating user 'typeeasy' ..."
  useradd --system --no-create-home --shell /usr/sbin/nologin typeeasy
fi

# --- Install binary + wrapper ---
echo "[install] Installing /usr/bin/typeeasy-server (interpreter) ..."
install -m 755 "$BINARY" /usr/bin/typeeasy-server

# Alias /usr/bin/typeeasy-bin -> typeeasy-server, en linea con .deb y CLI lookup
ln -sf typeeasy-server /usr/bin/typeeasy-bin

echo "[install] Installing /usr/bin/typeeasy (CLI wrapper) ..."
install -m 755 "$REPO_DIR/cli/typeeasy" /usr/bin/typeeasy

echo "[install] Installing /usr/bin/te (alias) ..."
ln -sf typeeasy /usr/bin/te

# --- Install templates ---
echo "[install] Installing /usr/share/typeeasy/templates/ ..."
mkdir -p /usr/share/typeeasy/templates
cp -r "$REPO_DIR/cli/templates/." /usr/share/typeeasy/templates/

# --- Install systemd unit ---
echo "[install] Installing systemd unit ..."
UNIT_PATH=/etc/systemd/system/typeeasy-api@.service
if [[ -f "$UNIT_PATH" ]] && ! cmp -s "$SCRIPT_DIR/typeeasy-api@.service" "$UNIT_PATH"; then
  BACKUP="${UNIT_PATH}.bak.$(date +%Y%m%d-%H%M%S)"
  echo "[install] WARNING: $UNIT_PATH ya existe y fue modificado."
  echo "[install]          Guardando copia en $BACKUP antes de sobrescribir."
  echo "[install]          Para cambios persistentes usa:  sudo systemctl edit typeeasy-api@<port>"
  cp -a "$UNIT_PATH" "$BACKUP"
fi
install -m 644 "$SCRIPT_DIR/typeeasy-api@.service" "$UNIT_PATH"
if [[ ! -f /etc/default/typeeasy-api ]]; then
  install -m 644 "$SCRIPT_DIR/typeeasy-api.default" /etc/default/typeeasy-api
  echo "[install] Created /etc/default/typeeasy-api (edit TYPEEASY_APIS_DIR there)"
fi

# --- Create runtime dirs ---
mkdir -p /opt/typeeasy/apis
chown -R typeeasy:typeeasy /opt/typeeasy

# --- Reload systemd ---
systemctl daemon-reload

cat <<EOF

============================================================
  TypeEasy instalado correctamente
============================================================

  Binario   : /usr/bin/typeeasy-server
  CLI       : /usr/bin/typeeasy
  Templates : /usr/share/typeeasy/templates/
  Endpoints : /opt/typeeasy/apis/
  Servicio  : /etc/systemd/system/typeeasy-api@.service

PROXIMOS PASOS:

  # 1. Copiar tus endpoints .te
  sudo cp mis_endpoints/*.te /opt/typeeasy/apis/
  sudo chown -R typeeasy:typeeasy /opt/typeeasy

  # 2. Activar el servicio (queda persistente)
  sudo systemctl enable --now typeeasy-api@8080

  # 3. Verificar
  systemctl status typeeasy-api@8080
  curl http://127.0.0.1:8080/api/health
  journalctl -u typeeasy-api@8080 -f

PARA MULTIPLES INSTANCIAS (por puerto):
  sudo systemctl enable --now typeeasy-api@8081
  sudo systemctl enable --now typeeasy-api@8082

PERSONALIZAR EL SERVICIO (User, WorkingDirectory, ProtectHome, etc):
  sudo systemctl edit typeeasy-api@8080
  # NO editar /etc/systemd/system/typeeasy-api@.service in-place:
  # dpkg/install.sh lo sobrescriben en cada upgrade.

EOF
