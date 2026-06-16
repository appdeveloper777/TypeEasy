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

## Conexión SQL Server TLS (cert self-signed)

El 6º argumento de `sqlserver_connect(host, user, pass, db, port, { ... })`
controla el TLS vía FreeTDS (db-lib).

| Archivo | Opción | Caso de uso |
|---------|--------|-------------|
| [sqlserver_tls_01_self_signed.te](sqlserver_tls_01_self_signed.te) | `{ "encrypt": 1, "tls_skip_verify": 1 }` | **SQL Server 2022 self-signed** — equivalente a `Encrypt=True;TrustServerCertificate=True` |

Claves aceptadas: `encrypt`/`tls`/`ssl` (`1`/`require`/`true` cifra; `0`/`off`
sin TLS), `tls_skip_verify` (alias `trust_server_certificate`, `trust_cert`,
`insecure`, `tls_no_verify`) para aceptar cert self-signed, y `tls_ca`/`ssl_ca`/`ca`
para validar contra un PEM propio. Si la conexión falla, la causa real queda
en la variable de script `__sqlserver_error__`.
