# 07_db_orm

Bridges nativos a bases de datos (mysql, postgres, sqlserver)
estilo Dapper. Requieren servicio externo configurado.

## Conexiones MySQL TLS (4 modos)

El 6º argumento de `mysql_connect(host, user, pass, db, port, { ... })`
controla el TLS. Los 4 ejemplos cubren cada caso:

| Archivo | Opción | Caso de uso |
|---------|--------|-------------|
| [mysql_tls_01_plano.te](mysql_tls_01_plano.te) | _(sin TLS)_ | Red privada/confiable; password cifrado vía RSA |
| [mysql_tls_02_ca_publica.te](mysql_tls_02_ca_publica.te) | `{ tls: 1 }` | Cloud con CA pública (TiDB, PlanetScale, Aiven, RDS) |
| [mysql_tls_03_fingerprint.te](mysql_tls_03_fingerprint.te) | `{ tls: 1, tls_fp: "<sha1>" }` | **Self-signed (recomendado)** — pin por huella, seguro |
| [mysql_tls_04_ca_insecure.te](mysql_tls_04_ca_insecure.te) | `{ tls: 1, tls_ca: "ca.pem" }` / `{ tls: 1, tls_insecure: 1 }` | CA propia, o sin verificar hostname (best-effort) |

Recomendación: para MySQL 8 con cert auto-generado self-signed usa
**`tls_fp`** (modo 3). Mantiene el canal cifrado y detecta MITM sin
instalar la CA en el sistema.
