# Deploy TypeEasy a un VPS Linux (producción)

Guía paso a paso para publicar APIs TypeEasy en un servidor Ubuntu/Debian con
systemd, dominio + SSL automático y resiliencia ante crashes/reboots.

> Stack final: **Caddy (HTTPS) → TypeEasy systemd (HTTP local) → MariaDB**

---

## 0. Pre-requisitos

| | |
|---|---|
| VPS | Ubuntu 22.04+ / Debian 12+ (cualquier proveedor: Hetzner $5, DigitalOcean, Linode, OVH) |
| Dominio | apuntando al IP del VPS (`api.midominio.com`) |
| Acceso | SSH como `root` o usuario con sudo |
| Local | TypeEasy ya compilado en `bin/typeeasy_api` |

---

## 1. Compilar el binario (en tu máquina o en el server)

```bash
# Local (recomendado: build una vez, deploy N veces)
cd TypeEasy
make            # produce bin/typeeasy_api (Linux ELF)

# O directo en el server
ssh root@mi-vps
apt update && apt install -y build-essential bison flex libmysqlclient-dev
git clone https://github.com/appdeveloper777/TypeEasy.git
cd TypeEasy && make
```

---

## 2. Subir el repo + instalar

```bash
# Desde local: copiar todo al server
rsync -avz --exclude='.git' --exclude='node_modules' \
  ./ root@mi-vps:/tmp/typeeasy-src/

# En el server: instalar
ssh root@mi-vps
cd /tmp/typeeasy-src
sudo ./installer/linux/install.sh
```

Esto deja todo listo:
- `/usr/bin/typeeasy-server` ← binario C
- `/usr/bin/typeeasy` ← CLI wrapper
- `/usr/share/typeeasy/templates/` ← templates de scaffolding
- `/etc/systemd/system/typeeasy-api@.service` ← unit instanciada
- `/etc/default/typeeasy-api` ← env vars
- `/opt/typeeasy/apis/` ← dónde poner los `.te`
- user/group `typeeasy` (system)

---

## 3. Crear tu primera app

### Opción A — usar el CLI scaffolder

```bash
cd ~
typeeasy new mi-app
cd mi-app
typeeasy gen resource producto
typeeasy gen resource pedido
typeeasy gen resource cliente
```

### Opción B — copiar `.te` que ya tenés

```bash
sudo cp ~/mis-endpoints/*.te /opt/typeeasy/apis/
sudo chown -R typeeasy:typeeasy /opt/typeeasy
```

---

## 4. Aplicar migraciones SQL

```bash
sudo apt install -y mariadb-server
sudo mysql_secure_installation       # contraseña root, etc.

# Crear DB + usuario
sudo mysql -u root -p <<SQL
CREATE DATABASE mi_app;
CREATE USER 'typeeasy'@'localhost' IDENTIFIED BY 'CONTRASEÑA_FUERTE';
GRANT ALL ON mi_app.* TO 'typeeasy'@'localhost';
FLUSH PRIVILEGES;
SQL

# Aplicar migrations generadas por el scaffolder
mysql -u typeeasy -p mi_app < migrations/*.sql
```

---

## 5. Configurar el servicio para que sirva TU directorio

Editar `/etc/default/typeeasy-api`:

```bash
sudo nano /etc/default/typeeasy-api
```
```bash
TYPEEASY_APIS_DIR=/opt/typeeasy/apis    # o /home/usuario/mi-app/apis
TYPEEASY_HOST=127.0.0.1                  # SOLO localhost (Caddy delante)
```

---

## 6. Arrancar el servicio (queda persistente)

```bash
sudo systemctl enable --now typeeasy-api@8080
sudo systemctl status typeeasy-api@8080
```

Lo que esto da:
- ✅ Arranca solo al bootear (`enable`)
- ✅ Auto-restart si crashea (en 2s)
- ✅ Multi-proceso (workers = nproc)
- ✅ Logs en journald (`journalctl -u typeeasy-api@8080`)
- ✅ Aislado como user `www-data`
- ✅ Hardening: `NoNewPrivileges`, `ProtectSystem`, `PrivateTmp`

Verificar localmente (sin SSL todavía):
```bash
curl http://127.0.0.1:8080/api/producto
```

---

## 7. Caddy delante (HTTPS automático)

```bash
sudo apt install -y caddy

sudo tee /etc/caddy/Caddyfile <<'EOF'
api.midominio.com {
    reverse_proxy 127.0.0.1:8080
    encode gzip
    log {
        output file /var/log/caddy/api.log
    }
}
EOF

sudo systemctl restart caddy
```

Caddy obtiene SSL de Let's Encrypt automáticamente.
Probar:
```bash
curl https://api.midominio.com/api/producto
```

---

## 8. Múltiples ambientes / múltiples apps en un VPS

```bash
# App de producción en :8080
sudo cp /etc/default/typeeasy-api /etc/default/typeeasy-api@8080
echo 'TYPEEASY_APIS_DIR=/opt/typeeasy/prod-apis' | sudo tee -a /etc/default/typeeasy-api@8080
sudo systemctl enable --now typeeasy-api@8080

# Staging en :8081
echo 'TYPEEASY_APIS_DIR=/opt/typeeasy/staging-apis' | sudo tee /etc/default/typeeasy-api@8081
sudo systemctl enable --now typeeasy-api@8081

# Caddy delante:
# api.midominio.com   → :8080
# staging.midominio.com → :8081
```

---

## 9. Deploy continuo desde tu máquina

Con el `typeeasy.toml` del proyecto:

```toml
[prod]
host       = "mi-vps.com"
user       = "deploy"
remote_dir = "/opt/typeeasy"
port       = 8080
```

Desde local:
```bash
typeeasy deploy
```

Eso hace `rsync ./apis/ → vps:/opt/typeeasy/apis/` + `systemctl restart typeeasy-api@8080`.

---

## 10. Operación día a día

```bash
typeeasy logs                # tail journald
typeeasy status              # estado
typeeasy restart             # reload manual
typeeasy stop                # apagar
```

Equivalentes nativos sin CLI:
```bash
journalctl -u typeeasy-api@8080 -f
systemctl status typeeasy-api@8080
sudo systemctl restart typeeasy-api@8080
```

---

## 11. Checklist de producción mínimo

| Item | Comando |
|---|---|
| Firewall | `sudo ufw allow OpenSSH && sudo ufw allow 'Caddy Full' && sudo ufw enable` |
| Backups DB | cron diario: `mysqldump mi_app > /backups/$(date +%F).sql` |
| Monitoring | UptimeRobot gratis apuntando a `https://api.midominio.com/api/health` |
| Updates | `sudo apt update && sudo apt upgrade -y` (mensual) |
| Logrotate journald | ya viene por default |

---

## 12. Troubleshooting

| Síntoma | Acción |
|---|---|
| Service no arranca | `journalctl -u typeeasy-api@8080 -n 100 --no-pager` |
| `Address already in use` | `sudo lsof -i :8080` y matar el proceso |
| Endpoint no responde | `curl http://127.0.0.1:8080/api/...` (saltar Caddy) |
| `apis_dir` vacío | revisar `/etc/default/typeeasy-api`, verificar que los `.te` existan y sean readable por `www-data` |
| MySQL connection refused | `sudo systemctl status mariadb`, revisar password en endpoint |
| SSL no funciona | `sudo journalctl -u caddy -n 100` (DNS mal apuntado es lo más común) |

---

## 13. Performance tips

- **Workers**: por default = `nproc`. Override con `TYPEEASY_WORKERS=4` en el env.
- **Cache TTL**: en cada endpoint `[HttpGet("...", CacheTtl=60)]` para cachear 60s.
- **Pool DB**: TypeEasy no tiene pooling nativo; abrir/cerrar conexión por request es OK hasta ~1000 rpm. Para más: poner ProxySQL delante de MariaDB.
- **HTTP/2**: Caddy lo da automático.
- **Compresión**: `encode gzip` en Caddy.
