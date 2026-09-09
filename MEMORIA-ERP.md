# MEMORIA COMPARTIDA — ERP TypeEasy (para todos los agentes agent1..5)

> **Un solo archivo compartido** por el repo principal (`ERP/`) y los worktrees (`ERP-agent1..5`),
> todos hermanos bajo `TypeEasy/`. Se lee por ruta relativa **`../MEMORIA-ERP.md`** desde cualquier
> worktree. Es la memoria "de equipo": convenciones, gotchas y hechos operativos verificados.
>
> ⚠️ **Sin credenciales.** Claves de QA/SSH/sync/BD **no** van acá (pedirlas a Fernando / están en el
> `.env` de cada entorno). Los sub-agentes `agent1..5` **solo preparan código** y **no despliegan**,
> así que no las necesitan.
>
> 📚 **Fuentes canónicas (leerlas SIEMPRE, están en cada worktree):**
> - `.github/agents/TypeEasy ERP.agent.md` — el agente **ERP Developer** base (arquitectura, comandos).
> - `SINTAXIS_Y_GOTCHAS.md` (raíz) — sintaxis y trampas del lenguaje `.te` (obligatorio antes de tocar `.te`).
> - `AGENTS.md` (raíz) — reglas del repo (contexto obligatorio, módulos, UI, licencias).
> - `docs/ESTANDAR_UI.md` — estándar visual del frontend Angular.

---

## 1) Qué es el proyecto
- **Frontend** Angular 21 (standalone + signals) en `frontend/src/app/`.
- **Backend** lenguaje **TypeEasy `.te`** en `modules/<key>/` + `apis/main.te` (generado).
- **BD** MariaDB (nube/VM y PC local) — motor STRICT. Capa `config/db.te` agnóstica de motor.
- Dominio: **POS/ERP** multipaís (Panamá ITBMS / Ecuador IVA), retail + restaurante, caja/arqueos,
  multi-sucursal, facturación electrónica DGI (PAC Factura Fácil), CxC, inventario, contabilidad, sync offline.

## 2) Gotchas críticos del lenguaje `.te` (los que más rompen)
> ⚠️ **2026-09-07:** el checklist de `SINTAXIS_Y_GOTCHAS.md` se reescribió en 3 listas: **(A) reglas del lenguaje**, **(B) vigentes**
> (MariaDB STRICT, `@param` sin afinidad, single-flight…), **(C) resueltos por versión de TE**. Los ítems
> **0.0.33** (frames de fn = causa real de #38/30c, `[]` fresco por llamada, `(""+map)`→JSON, `lista[i]` anidada, errores con
> `archivo:línea`) están **INSTALADOS desde 2026-09-08** en la VM (15 backends) y en `%LOCALAPPDATA%`: sus workarounds ya no
> hacen falta en código nuevo. Los puntos de abajo marcados ✅ quedan como historia; los ⚠️ siguen vigentes.
> **2026-09-08:** los **15 backends** (PROD 8090, qa, xp, 12 demos) corren con **`--workers 2`** (prefork SO_REUSEPORT; bench
> +35 % rps / −28 % p50, y una request colgada ya no congela a todos). Consecuencia: **NO agregar estado mutable global** al
> `.te` (contadores, caches, rate-limit en memoria) — cada worker tendría su copia; usar la BD. Backups: `*.service.bak-w1-20260908`.
> **0.0.34 (tag `v0.0.34` = `f56e98c`, INSTALADO 2026-09-08 en VM + local):** `for (var i=0; i<n; i++)` estilo Java, `--syntax-check` semántico (detecta reasignar
> `let`, comparación en `for` clásico), `--profile`. Ya se puede usar el `for` Java en el ERP. Fix win64: el 500 por error fatal ya no tumba el server
> (`_setjmp(buf, NULL)`, regresión de `--profile` en `cce457e`). Backups: VM `/usr/bin/typeeasy-bin.bak-20260908-76cc519c`, local `typeeasy-bin.exe.bak-0.0.33-174839d0`.
> **0.1.0 (tag `v0.1.0` = `88a6ee8`, INSTALADO 2026-09-09 en VM (15 backends, md5 `b3cc2a2a`) + local):** plan de deuda del núcleo **completo** (Fases 0–4).
> Para el ERP: **enteros de 64 bits exactos** (`let a = 3000000000` ya no da negativo; `@param` int64 y filas MySQL sin truncar → ya se pueden
> calcular ids/epoch-ms/montos grandes en `.te`), recovery completa tras un 500 fatal (ningún flag "en vuelo" pasa al siguiente request),
> `typeeasy --selftest-vm`. Semántica del lenguaje **sin cambios**. ⚠️ El primer build de 0.1.0 (bc3a31f, 2026-09-08) tenía roto el **debugger de
> VS Code** (vista de variables): si alguien lo instaló localmente, reinstalar. Backups: VM `/usr/bin/typeeasy-bin.bak-*-010a`, BD
> `/home/azureuser/backup_erp_pre010b_*.sql.gz`, local `typeeasy-bin.exe.bak-0.1.0a-*`.
- `let` = **CONSTANTE**, `var` = mutable. Reasignar un `let` → error **en runtime** (el `--syntax-check` NO lo detecta). Los default-value reassignment (`if (x=="") { x=... }`) exigen `var`.
- `fn` anónima con flecha: `let f = fn(a) => { ... };`. Métodos requieren tipo de retorno. **No hay `super`.**
- `for` clásico: `for(i=INIT; LÍMITE; PASO)` con **límite EXCLUSIVO** y **PASO** (no condición). No existe `for(i=0;i<4;i++)`.
- **`from` es palabra reservada**: no puede ser clave de map ni `@from` param → renombrar (`vfrom`). `--syntax-check` puede no verlo; `build-modules` con `TYPEEASY_BIN` **excluye el módulo entero** (404).
- `json_parse` → acceso por **corchetes** `p["k"]`. **Nunca encadenar** `x[a]["k"]` en una comparación/condición → extraé cada nivel a un `let` primero. Materializar null: `var v=(""+p["x"]); if(v=="null"){v="";}`.
- **Buffer aliasing**: `request_param()` + `session_nick()` pasados **inline** como args de la MISMA llamada COLAPSAN (comparten buffer). Materializar cada uno en un `let` antes de llamar.
- `db_exec` devuelve el **envelope** `{success, error, data}` (NO hacer `res.contains(...)` sobre él → segfault). Usar `let res = db_exec(...)` (no pre-declarar `var res={...}` y reasignar → 400 falso) + `db_result_or_4xx(res)`.
- SQL: el 4º arg `"json"` de `mysql_*` **solo en SELECT**. `ON DUPLICATE KEY` → branch por motor (`if (DB_ENGINE=="sqlite") { ... ON CONFLICT ... }`). MySQL STRICT: `''` en columna DATE/INT rompe el INSERT → `NULLIF(@x,'')` / `COALESCE(NULLIF(@x,''),<def>)`. `NULLIF(@num,'')` sobre TINYINT da "Truncated DECIMAL" → computar el default en `.te`.
- ✅ Ya RESUELTOS en el binario 0.0.30 (no temerles): SIGSEGV en handlers pesados, INSERT que colgaba en STRICT, `x=(comparación)/índice/ternario` que guardaba 0, `--syntax-check` valida aridad, `json(fnCall())`.
- **Validar SIEMPRE**: `"$LOCALAPPDATA/Programs/TypeEasy/bin/typeeasy-bin.exe" --syntax-check <archivo.te>` → `{"ok":true,"errors":[]}`.

## 3) Arquitectura de módulos (estilo Odoo/plugins)
- `modules/<key>/module.json` = manifest (key, label, icon, order, enabled, required, depends, backend[], menu[], routes[]).
- `tools/build-modules.mjs` (Node) lee los manifests, topo-ordena por `depends` y **REGENERA** 4 archivos con banner "NO EDITAR A MANO": `apis/main.te`, `apis/sys.generated.te`, `frontend/src/app/app.routes.ts`, `frontend/src/app/modules.generated.ts`. **Los sub-agentes NO corren build-modules** (lo hace el integrador).
- **Regla de oro**: cada módulo solo crea/altera tablas con su prefijo `<key>_*`. Para extender una tabla ajena (p.ej. `inv_productos`) → **tabla satélite** `<key>_*` enlazada por la PK; **nunca** un ALTER directo al core.
- **Migraciones**: `modules/<key>/migrations/NNN_desc.sql` con sección `-- @down`. Idempotentes (`CREATE TABLE IF NOT EXISTS`, `ADD COLUMN IF NOT EXISTS`, `INSERT IGNORE`). Control en `erp_migrations` (id `<key>:NNN_...sql`). **Los sub-agentes NO aplican migraciones** a la BD compartida.
- Backend en 3 archivos por módulo (patrón repository): `<key>_model.te` (clases de model binding, todas string) → `<key>_service.te` (repo `Repo_*`, TODA la SQL, devuelve JSON-string) → `<key>.te` (controlador delgado: `return json(Repo_X(...))`). En ese orden en `backend[]`.
- **Auth**: cookies `user`+`logkey`. Guard `@requiere_sesion` antes de un bloque `endpoint { }`. Health/login públicos en bloque SIN decorador.

## 4) Frontend (Angular)
- Standalone + signals. Componentes en `frontend/src/app/modules/<key>/`.
- **Estándar UI** (docs/ESTANDAR_UI.md): encabezado compacto (h2 19px/800 + breadcrumb + subtítulo), **pestañas folder de Windows** (gris `#dde2e9`/negro inactivas, blanca/azul `#0078D4` activa, conectadas al cuerpo con `border-top:0`), **una tarjeta blanca** con **chips de sección azules** (`#dcecf8`/`#b9d6ea`/`#0e3157`), dropdowns largos → buscador. Dark-mode SOLO con **`:host-context(body.dark)`** (NO `body.dark`, rompe encapsulación).
- **Orden (sort) en tablas paginadas — TODAS iguales a `/ventas/clientes`** (docs/ESTANDAR_UI.md §14): el look ya es GLOBAL (`frontend/src/styles.css`, bloque "Gridview estandar": `thead th` degradado + `thead th.sortable.active` azul + hover + regla del ícono `thead th.sortable .bi`) → NO cambiar la clase de la `<table>`, solo marcar cabeceras `<th class="sortable" [class.active]="sortCol()==='col'" (click)="sortBy('col')">Etiqueta <i [class]="'bi ' + sortIco('col')"></i></th>`. Ícono estándar = **FLECHAS** (`sortIco` → `bi-arrow-down-up sort-idle`/`bi-arrow-up`/`bi-arrow-down`; NO carets ni chevrons). **Inicializar `sortCol` con una columna por defecto** (nunca `''`) para que el grid **cargue con una columna activa** como clientes (listas con fecha → `'fecha'`/`'desc'`; catálogos → `'nombre'`/`'asc'`). Backend-paginada: el `.te` acepta `sort`/`dir` (query) con **lista blanca** en el `ORDER BY` (nunca interpolar crudo). Client-side (traen todo, cap 200/500): envolver el computed filtrado con un helper `ordenar<T>(arr)` (fecha→parse, numérico→parseFloat, texto→`localeCompare('es',{numeric:true})`), reiniciar al cambiar sub-tabla. Refs: `clientes-list`/`productos-list`.
- HTTP: `HttpClient`, endpoints `/api/<modulo>/...`. Servicios con `providedIn:'root'`.
- Model binding: las clases `.te` exigen **todos** los campos presentes → el front debe mandarlos todos (o 422). Mandar strings (el SQL convierte).

## 5) Entorno / comandos (Windows, Git Bash) — referencia (los sub-agentes NO despliegan)
- Binario TE: `"$LOCALAPPDATA/Programs/TypeEasy/bin/typeeasy-bin.exe"`. Node real (captura salida): `/c/nodejs/node.exe` (el `node` del PATH es un shim roto con pipes).
- **build-modules** (SOLO el integrador): `TYPEEASY_BIN="$LOCALAPPDATA/Programs/TypeEasy/bin/typeeasy-bin.exe" /c/nodejs/node.exe tools/build-modules.mjs 2>&1 | grep -iE "exclu|error|activos"` → debe listar "activos (N): ..." y **NINGÚN "EXCLUIDO"**.
- **Frontend build** (SOLO el integrador): `cd frontend && node_modules/.bin/ng build --configuration production --output-path=dist/erp-frontend` (~2-4 min).
- **VM** de producción: `azureuser@172.210.9.14` (SSH por llave). Backend systemd `erp-backend` :8090; nginx sirve `/opt/erp/frontend/dist/erp-frontend/browser`. DB `sudo mysql erp` (MariaDB STRICT). Health: `curl 127.0.0.1:8090/api/pos/health`. **Credenciales: con Fernando.**
- ⚠️ **Deploy frontend NO debe borrar `browser/store/`** (storefront / vitrina B2C). `/tienda-en-linea` NO es ruta Angular: nginx lo sirve con `location = /tienda-en-linea { alias .../browser/store/index.html; }` (la ruta del panel es `/ecommerce`). El `ng build` NO genera `store/`; si el deploy hace `rm -rf browser/*` + tar SIN `store/` → **borra la vitrina → 404**. ANTES de tar, copiar SIEMPRE `cp storefront/index.html storefront/app.js frontend/dist/erp-frontend/browser/store/`. Verificar: `curl -sL https://panamasoft.duckdns.org/tienda-en-linea | grep '<title>Tienda en línea'`.
- **Pruebas curl / QA navegador**: usuario dedicado `qa` (admin=1) — **clave: pedir a Fernando** (NO usar admin2/admin3/cajero1 del humano: el login rota su `log_key` y tumba su sesión del navegador).

## 6) Flujo de trabajo PARALELO (agent1..5)
0. **Revisá los adjuntos del ticket antes de codear** (imágenes, capturas, PDFs, documentos que suban José/Darwinson/Fernando en la tarjeta de Trello): abrí/descargá cada uno con `trello/*` y mirá su contenido — el detalle real (layout esperado, dato que falta, error, formato de factura, mockup) suele estar en la imagen/PDF, **no** en el texto. El **integrador** hace lo mismo al **code review** y al **QA en el navegador** (contrasta lo que ve en pantalla contra los adjuntos).
   - **Cómo VER un adjunto de Trello (verificado):** `get_card` da la URL `.../download/x.png`; el `?key=&token=` por query da **401** → descargá con el header OAuth leyendo el token de `.vscode/mcp.json` **sin imprimirlo** y veéla con `view_image` en su path Windows real (no entiende `/tmp`): `KEY=$(grep -o '"TRELLO_API_KEY": "[^"]*"' .vscode/mcp.json | sed 's/.*: "//;s/"$//'); TOK=$(grep -o '"TRELLO_TOKEN": "[^"]*"' .vscode/mcp.json | sed 's/.*: "//;s/"$//'); curl -sL -H "Authorization: OAuth oauth_consumer_key=\"$KEY\", oauth_token=\"$TOK\"" "<URL>" -o /tmp/adj.png` → `file /tmp/adj.png` (debe decir "image") → `cd /tmp && pwd -W` da la ruta Windows para `view_image`. ⚠️ NUNCA imprimas el token.
1. Trabajás en **tu branch** (`agentN`, worktree `ERP-agentN`). No cambiás de branch. No `push`/`merge`.
2. **Solo preparás código**: editás `.te`/`.ts`/`.html`/`.css`/`.sql`, creás migraciones, y validás sintaxis local. **NO** corrés build-modules, **NO** compilás Angular, **NO** desplegás a la VM, **NO** aplicás migraciones a la BD compartida, **NO** QA contra la VM.
3. Cambios **mínimos y locales** (diffs pequeños), sin refactor de más.
4. Al terminar: **commit en tu branch** con un mensaje que **INCLUYA el número de ticket** (ej. `feat(pos): descripción (#34)` / `fix(inventario): … (#37)`), listá los archivos tocados, syntax-check en verde, y **avisá si tu ticket choca** con archivos "calientes" (`apis/main.te`, `modules/pos/pos_service.te`, los 4 generados) para que el integrador serialice esa parte.
5. El **integrador** (ventana principal / Fernando) hace UNA vez: merge de branches → build-modules (0 exclusiones) → migraciones en orden → un build → un deploy → un QA.
6. **Comentarios en Trello = PARA EL CLIENTE, no técnicos.** El que lo lee es José/Darwinson (negocio), no un dev. **Arrancá diciendo claro si el pedido FUNCIONA / NO funciona aún / quedó parcial** (en palabras del requerimiento), qué puede hacer ahora el usuario y qué falta. **NADA de detalles técnicos** (archivos, endpoints, códigos http, commits, migraciones, logs, tcpdump) salvo que los pidan. Un comentario responde "¿funcionó o no?", no el "cómo". Ej.: en vez de «token_digifact_http_0, no abre socket» → «la factura todavía NO se envía por Digifact».

## 7) Datos/hechos útiles recientes (verificados)
- **Maestro de productos** = `inv_productos` (un PLATO de restaurante = `es_servicio=1`). Categoría del POS = **`grupo`** del producto (`inv_grupos` ↔ `pos_categorias` por NOMBRE). "Departamento" de la ficha (`inv_departamentos`) es un eje de inventario **aparte** que el POS no usa.
- **Impuesto por país** snapshot en la venta (PA ITBMS 7% / EC IVA 15%). Venta one-shot `POST /api/pos/ventas/sync` (dedup por `client_uuid`).
- **Módulo `produccion`** (Fase 1, conversión de unidades): tablas `prod_unidades` (categoría+factor_base), `prod_conversiones` (globales `codpro=''` o por producto), `prod_producto_unidades` (satélite), `prod_config`. Motor de conversión hace la aritmética **en SQL** (evita parseo numérico en `.te`).
- **Facturación electrónica** (PAC Factura Fácil): `receptor.type` 01=Contribuyente, 02=Consumidor final, 03=Gobierno, 04=Extranjero (con `country`=alpha-2 para pasaporte). `taxes.type` 01=ITBMS. `gns`=código CPBS (obligatorio si receptor 03). Docs PAC en `docs/referencia-pac/`.
- **Instalador local** (`installer/dist/ERP-Local/`): stack autocontenido (MariaDB+Node+TE+app). `empaquetar-zip.ps1` genera `ERP-Local.zip`. `build-package.ps1` NO usar con instancia local corriendo (borra `app/` antes de fallar en `data/` bloqueado).

## 8) Venta en la nube (Niels — nielssoftware.com) — 2026-09-01, VALIDADO CON PAGO REAL
- **Pipeline 100% automático**: landing → `POST /api/checkout` (licenciador :8099) → link PagueloFacil prod → pago → webhook → `procesar-ordenes.sh` (timer 1 min) → `provisionar-cliente.sh` → tenant listo en ~46 s (BD `erp_c_<slug>` limpia + systemd `erp-cliente-<slug>` :8200-8299 + subdominio TLS + licencia 35d/370d + correo de bienvenida + credenciales en /gracias). Primer pago REAL e2e OK (orden 23 → mi-market27).
- **La orden captura**: plan (caja 29/negocio 59/empresa 99), ciclo, vertical (especialidad), **país (PA/EC/NI/CU/BO/CO)** y **módulo extra opcional**. El aprovisionador aplica: `pos_config.pais_activo` (impuesto correcto por país, QA verificado: PA ITBMS 7 / EC-NI IVA 15 / BO 13 / CO 19 / CU exento), **módulos visibles según plan** (caja SIN contabilidad/tienda/nómina; negocio +contabilidad+tienda; empresa todo; lo elegido en vertical/extra siempre queda), `usa_sucursales=1` solo negocio/empresa, y `plan_contratado` (migración pos 090: con plan `caja` el frontend OCULTA la sección Sucursales de /settings).
- **Aislamiento por tenant** (2026-09-01): usuario MySQL propio `u_<slug>` (el usuario `erp` quedó revocado de las bases de clientes), sandbox systemd (ProtectSystem=strict, MemoryMax=512M, CPUQuota=80%), rate-limit nginx (/api/ 15r/s; login 2r/s), backups diarios 03:30 (`/opt/backups-db/diarios/`, retención 7d). Scripts en `deploy/clientes/`.
- **FE por país**: solo Panamá tiene integración real (PAC/DGI). La tabla de precios lo aclara ("otros países: comprobantes de venta"). Bolivia (SIAT) NO integrado — el target boliviano (RTS) no factura.
- **Diagnóstico de ventas**: tabla `checkout_log` en licencias.db (eventos CHECKOUT/PF_LINK/WEBHOOK). Gotcha PF: tarjeta rechazada → su página "Error 401", no valida inline; el link dura 1h.
- **Respaldo local de secretos de la VM**: `LICENCIAS/vm-secrets/` (git-ignorado) con `.env`, `licencias.db` y llaves Ed25519 — refrescar periódicamente.

---
*Mantenida por el agente integrador (ventana principal). Los agent1..5 la LEEN; si descubren un gotcha nuevo, lo reportan al integrador para que la actualice (evita choques de escritura concurrente).*
