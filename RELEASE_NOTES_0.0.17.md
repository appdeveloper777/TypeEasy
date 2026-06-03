## TypeEasy v0.0.17

Esta versión añade **opciones TLS por conexión para MySQL** (certificados
self-signed sin tocar el trust store del sistema) y consolida el decorador
**`@auth`** para proteger endpoints con JWT.

### Novedades

- **MySQL TLS por conexión** — nuevas opciones en el 6.º argumento de `mysql_connect`:
  - `tls_fp` — pin del certificado por huella **SHA1** (`MARIADB_OPT_TLS_PEER_FP`). Modo recomendado para servidores self-signed: cifra el canal y detecta MITM sin instalar ninguna CA.
  - `tls_ca` — valida la cadena contra un **PEM propio**.
  - `tls_insecure` (alias `tls_no_verify`, `insecure`, `tls_skip_verify`) — omite la verificación de hostname (best-effort).
  - El camino por defecto `{ tls: 1 }` **no cambia** (CA pública del sistema, como TiDB / PlanetScale / RDS). Sin regresiones.
- **`@auth endpoint { }`** — protege todas las rutas de un bloque con un solo decorador, sin repetir la validación del Bearer token en cada handler.

### Ejemplo 1 — `@auth`: por action vs. a nivel de bloque (JWT)

```typeeasy
// Bloque PUBLICO: login -> devuelve el token (sin @auth)
endpoint {
    [HttpPost("/api/login")]
    login(u : User) {
        let secret  = env("JWT_SECRET");
        let exp     = now_epoch() + 3600;          // vence en 1 hora
        let payload = concat("{\"sub\":\"", u.name, "\",\"role\":\"admin\",\"exp\":", exp, "}");
        let token   = jwt_sign(payload, secret);
        return json({ token: token });
    }
}

// FORMA A: @auth por ACTION — protegés ruta por ruta dentro de un bloque.
endpoint {
    @auth
    [HttpGet("/api/perfil")]
    perfil() {
        // current_claims() = JSON validado del token
        return json(concat("{\"ok\":true,\"claims\":", current_claims(), "}"));
    }

    // sin @auth -> esta ruta queda PUBLICA aunque esté en el mismo bloque.
    [HttpGet("/api/ping")]
    ping() {
        return json("{\"pong\":true}");
    }
}

// FORMA B: @auth GENERAL (a nivel de bloque) — protege TODAS las rutas,
// sin repetir el decorador en cada action.
@auth endpoint {
    [HttpGet("/api/secreto")]
    secreto() {
        return json("{\"dato\":\"solo visible con token valido\"}");
    }

    [HttpGet("/api/admin")]
    admin() {
        return json("{\"msg\":\"panel admin, protegido por el @auth del bloque\"}");
    }
}
```

Uso:

```bash
# 1) login -> token
curl -s -X POST localhost:8080/api/login -d '{"name":"ana","age":30}'

# 2) ruta protegida con el token
curl -s localhost:8080/api/perfil -H "Authorization: Bearer <token>"
```

### Ejemplo 2 — Conexión MySQL TLS con cert self-signed (`tls_fp`)

```typeeasy
endpoint {
    [HttpGet("/api/usuarios")]
    GetUsuarios() {
        // Pin por huella SHA1: cifra el canal y valida el cert sin CA en el sistema.
        let conn = mysql_connect(
            "mi-host", "usuario", "***", "mi_db", 3306,
            { tls: 1, tls_fp: "5ff1330b35b36db1cbeafeb890294ec054d4f74c" }
        );
        if (conn < 0) { return json("{\"error\":\"sin conexion\"}"); }

        let r = mysql_query(conn, "SELECT VERSION() AS version, DATABASE() AS db", "json");
        mysql_close(conn);
        return json(r);
    }
}
```

Cómo obtener la huella SHA1 del servidor:

```bash
openssl s_client -connect HOST:3306 -starttls mysql </dev/null 2>/dev/null \
  | openssl x509 -noout -fingerprint -sha1
# quitar los ':' y pasar en minúsculas
```

**Los 4 modos de conexión** (sin TLS / CA pública / `tls_fp` / `tls_ca`·`tls_insecure`)
están documentados con ejemplos ejecutables en `examples/07_db_orm/`.

**Full Changelog**: v0.0.15...v0.0.17
