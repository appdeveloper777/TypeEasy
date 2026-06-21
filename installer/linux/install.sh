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
#   sudo ./installer/linux/install.sh --vscode-ext      # instala extensión VS Code sin preguntar
#   sudo ./installer/linux/install.sh --no-vscode-ext   # no preguntar ni instalar la extensión
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
VSCODE_EXT="auto"   # auto = prompt if 'code' present; yes = always; no = never
while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    --vscode-ext) VSCODE_EXT="yes"; shift ;;
    --no-vscode-ext) VSCODE_EXT="no"; shift ;;
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

# --- Install bundled VS Code extension (.vsix), if present ---
# El .vsix lo empaqueta scripts/package_linux_release.sh en $REPO_DIR/vscode/
# (o $REPO_DIR/share/typeeasy/vscode/ según el layout). Lo dejamos en
# /usr/share/typeeasy/vscode para que `te ext install` y el bloque de abajo
# lo encuentren.
VSIX_BUNDLED=""
for cand in \
  "$REPO_DIR/vscode" \
  "$REPO_DIR/share/typeeasy/vscode" \
  "$SCRIPT_DIR/../../vscode"; do
  [[ -d "$cand" ]] || continue
  v="$(ls "$cand"/*.vsix 2>/dev/null | head -n1 || true)"
  [[ -n "$v" ]] && { VSIX_BUNDLED="$v"; break; }
done
if [[ -n "$VSIX_BUNDLED" ]]; then
  echo "[install] Installing VS Code extension package -> /usr/share/typeeasy/vscode/ ..."
  mkdir -p /usr/share/typeeasy/vscode
  install -m 644 "$VSIX_BUNDLED" "/usr/share/typeeasy/vscode/$(basename "$VSIX_BUNDLED")"
fi

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

# --- Optional: install VS Code extension for the invoking user -------------
# VS Code extensions son per-user. Como este script corre como root via sudo,
# usamos $SUDO_USER (si existe) para invocar 'code --install-extension' bajo
# su HOME. Solo se ejecuta si:
#   - hay un .vsix bundleado en /usr/share/typeeasy/vscode/
#   - el usuario tiene 'code' en su PATH (VS Code instalado)
#   - --vscode-ext (autoexplícito) o --no-vscode-ext NO fue pasado, y
#     en modo interactivo respondió 'y' al prompt.
maybe_install_vscode_ext() {
  [[ "$VSCODE_EXT" == "no" ]] && return 0
  local target_user="${SUDO_USER:-}"
  [[ -z "$target_user" || "$target_user" == "root" ]] && return 0
  local vsix
  vsix="$(ls /usr/share/typeeasy/vscode/*.vsix 2>/dev/null | head -n1 || true)"
  [[ -z "$vsix" ]] && return 0
  # Verifica 'code' bajo el usuario invocador (no bajo root).
  if ! sudo -u "$target_user" -H bash -lc 'command -v code >/dev/null 2>&1'; then
    echo "[install] (skip) VS Code CLI 'code' no está en el PATH de $target_user; usa 'te ext install' luego."
    return 0
  fi
  if [[ "$VSCODE_EXT" == "auto" ]]; then
    if [[ ! -t 0 ]]; then
      echo "[install] (skip) modo no-interactivo y sin --vscode-ext; usa 'te ext install' o re-ejecuta con --vscode-ext."
      return 0
    fi
    read -r -p "[install] ¿Instalar extensión TypeEasy en VS Code de $target_user? [y/N] " reply
    case "$reply" in y|Y|yes|YES) ;; *) return 0 ;; esac
  fi
  echo "[install] Installing VS Code extension as $target_user ..."
  if sudo -u "$target_user" -H bash -lc "code --install-extension '$vsix' --force"; then
    echo "[install] OK. Reinicia VS Code para activar el resaltado de .te y F5 debug."
  else
    echo "[install] WARN: 'code --install-extension' falló; instala manualmente con 'te ext install'."
  fi
}
maybe_install_vscode_ext

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
