# Biblioteca estándar de TypeEasy — referencia con ejemplos

> Todos los ejemplos de esta página son **tests ejecutables**: el bloque de código es
> el archivo `.te` citado y la salida es su `.expected`. Si un ejemplo deja de funcionar,
> la suite `tests/lang` falla en CI. Verificado con **TypeEasy 0.1.9**.
>
> Para HTTP (request/response, JWT, cookies), base de datos y entorno ver
> [API_BUILTINS.md](API_BUILTINS.md). Para la semántica del lenguaje ver [SPEC.md](SPEC.md).

## 1. Strings — [`tests/lang/07_stdlib/ref_strings.te`](../tests/lang/07_stdlib/ref_strings.te)

| Método | Devuelve | Nota |
|---|---|---|
| `s.replace(a, b)` | string | reemplaza **todas** las ocurrencias |
| `s.index_of(x)` / `s.find(x)` | int | `-1` si no está |
| `s.starts_with(x)` / `s.ends_with(x)` | bool (`1`/`0`) | |
| `s.substring(ini, fin)` | string | `fin` exclusivo |
| `s.substr(ini, largo)` | string | por longitud |
| `s.pad_left(n, c)` / `s.pad_right(n, c)` | string | útil para correlativos (`"007"`) |
| `s.repeat(n)` | string | |
| `s.char_at(i)` / `s.char_code()` | string / int | |
| `s.parse_int()` / `s.parse_float()` | int / float | |
| `s.split(sep)` | lista | |
| `s.upper()` / `s.lower()` / `s.trim()` | string | |
| `s.contains(x)` | bool | |

```te
let s = "Hola Mundo";
println("replace=" + s.replace("Mundo", "TE"));
println("index_of=" + s.index_of("Mundo") + " find=" + s.find("o") + " falta=" + s.index_of("xyz"));
println("starts=" + s.starts_with("Hola") + " ends=" + s.ends_with("do"));
println("substring=" + s.substring(0, 4) + " substr=" + s.substr(5, 3));
println("pad_left=" + "7".pad_left(3, "0") + " pad_right=" + "ab".pad_right(4, "."));
println("repeat=" + "-".repeat(5));
println("char_at=" + s.char_at(1) + " char_code=" + "A".char_code());
println("parse_int=" + ("42".parse_int() + 1) + " parse_float=" + ("2.5".parse_float() * 2));
let partes = "a,b,c".split(",");
println("split=" + partes.length + " primero=" + partes[0]);
println("upper=" + s.upper() + " lower=" + s.lower() + " trim=[" + "  x  ".trim() + "]");
```

```text
replace=Hola TE
index_of=5 find=1 falta=-1
starts=1 ends=1
substring=Hola substr=Mun
pad_left=007 pad_right=ab..
repeat=-----
char_at=o char_code=65
parse_int=43 parse_float=5
split=3 primero=a
upper=HOLA MUNDO lower=hola mundo trim=[x]
```

Interpolación: `$"Total: {a + b}"` (ver `tests/lang/07_stdlib/string_interp_expr.te`).

## 2. Listas — [`tests/lang/07_stdlib/ref_lists.te`](../tests/lang/07_stdlib/ref_lists.te)

| Método | Nota |
|---|---|
| `l.push(x)` | agrega al final (muta) |
| `l.length` / `l.size()` / `l.get(i)` / `l[i]` | |
| `l.contains(x)` | bool |
| `l.join(sep)` | string |
| `l.sort()` / `l.reverse()` | **mutan en el lugar y no devuelven nada** |
| `l.map(fn)` / `l.filter(fn)` / `l.reduce(fn, inicial)` | devuelven valor nuevo |
| `for (let x in lista) { ... }` | el `let` es obligatorio |

```te
var l = [3, 1, 2];
l.push(5);
println("length=" + l.length + " size=" + l.size() + " get=" + l.get(0));
println("contains=" + l.contains(2) + " no=" + l.contains(9));
println("join=" + l.join("-"));
l.sort();
println("sort=" + l.join(","));
l.reverse();
println("reverse=" + l.join(","));
let dobles = [1, 2, 3].map(fn(x) => x * 2);
let pares = [1, 2, 3, 4].filter(fn(x) => x % 2 == 0);
let suma = [1, 2, 3, 4].reduce(fn(a, x) => a + x, 0);
println("map=" + dobles.join(",") + " filter=" + pares.join(",") + " reduce=" + suma);
var total = 0;
for (let x in [10, 20, 30]) {
    total = total + x;
}
println("for_in=" + total);
```

```text
length=4 size=4 get=3
contains=1 no=0
join=3-1-2-5
sort=1,2,3,5
reverse=5,3,2,1
map=2,4,6 filter=2,4 reduce=10
for_in=60
```

## 3. Maps — [`tests/lang/07_stdlib/ref_maps.te`](../tests/lang/07_stdlib/ref_maps.te)

| Método | Nota |
|---|---|
| `m["k"] = v` / `m["k"]` | clave faltante → vacío/`null` |
| `m.size()` / `m.length` | |
| `m.has(k)` | bool |
| `m.keys()` / `m.values()` | listas, en orden de inserción |
| `m.remove(k)` / `m.clear()` | mutan |

```te
var m = { "nombre": "Ana", "edad": 30 };
m["ciudad"] = "Lima";
println("size=" + m.size() + " length=" + m.length);
println("has=" + m.has("edad") + " no=" + m.has("pais"));
println("keys=" + m.keys().join(","));
let vs = m.values();
println("values=" + len(vs));
m.remove("edad");
println("tras_remove=" + m.size() + " has_edad=" + m.has("edad"));
println("falta=[" + m["pais"] + "]");
m.clear();
println("tras_clear=" + m.size());
```

```text
size=3 length=3
has=1 no=0
keys=nombre,edad,ciudad
values=3
tras_remove=2 has_edad=0
falta=[]
tras_clear=0
```

## 4. Conversiones, Math y JSON — [`tests/lang/07_stdlib/ref_conv_math.te`](../tests/lang/07_stdlib/ref_conv_math.te)

`to_int`, `to_float`, `to_str`, `len`, `range(ini, fin)` (fin exclusivo),
`Math.floor/ceil/round/trunc/abs/sign/pow/sqrt/min/max/mod`, `json_stringify`, `json_parse`
(acceso con corchetes `p["k"]`). Para dinero exacto usar el tipo `decimal` (ver SPEC.md).

```te
println("to_int=" + (to_int("12") + 1) + " to_float=" + to_float("1.5") + " to_str=" + to_str(7) + "!");
let lista = [1, 2, 3];
println("len_str=" + len("hola") + " len_list=" + len(lista));
println("range=" + range(0, 4).join(","));
println("floor=" + Math.floor(3.7) + " ceil=" + Math.ceil(3.2) + " round=" + Math.round(2.5) + " trunc=" + Math.trunc(-3.7));
println("abs=" + Math.abs(-5) + " sign=" + Math.sign(-2) + " pow=" + Math.pow(2, 10) + " sqrt=" + Math.sqrt(16));
println("min=" + Math.min(3, 8) + " max=" + Math.max(3, 8) + " mod=" + Math.mod(10, 3));
println("json=" + json_stringify({ "a": 1, "b": [true, null] }));
let p = json_parse("{\"x\": 5, \"y\": \"z\"}");
println("json_parse=" + p["x"] + p["y"]);
```

```text
to_int=13 to_float=1.5 to_str=7!
len_str=4 len_list=3
range=0,1,2,3
floor=3 ceil=4 round=3 trunc=-3
abs=5 sign=-1 pow=1024 sqrt=4
min=3 max=8 mod=1
json={"a":1,"b":[true,null]}
json_parse=5z
```

## 5. Trampas conocidas (cada una tiene su test `// xfail`)

Regla del proyecto: **una trampa sin test no se documenta**. Cuando se corrija en el motor,
el test pasa a `XPASS` y se quita el `xfail`.

| Trampa (0.1.9) | Workaround | Test |
|---|---|---|
| `len([1,2,3])` sobre una lista **literal** devuelve `0` | guardar la lista en una variable | `tests/lang/13_gotchas/len_list_literal.te` |
| `m.values().length` devuelve `0` | `let vs = m.values(); len(vs)` | `tests/lang/13_gotchas/map_values_length.te` |
| `Math.PI` / `Math.E` no existen | `let PI = 3.141592653589793;` | `tests/lang/13_gotchas/math_pi_constant.te` |
| `l.sort()` no devuelve la lista | llamar y luego usar `l` | `ref_lists.te` |

## 6. Cómo agregar una entrada a esta página

1. Escribí el ejemplo como test en `tests/lang/07_stdlib/ref_<tema>.te` (con `// spec: STD-...`).
2. Generá la salida con el binario y revisala: `typeeasy ref_<tema>.te > ref_<tema>.expected`.
3. Copiá el código y la salida acá. `python tools/te-test/run_tests.py tests/lang --bin <typeeasy>` debe dar PASS.
