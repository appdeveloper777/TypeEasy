"""Genera productos.xlsx — dataset minimo para el ejemplo 06b_xlsx.

Uso:
    python crear_productos_xlsx.py
"""

from pathlib import Path

from openpyxl import Workbook


def main() -> None:
    wb = Workbook()
    ws = wb.active
    ws.title = "productos"
    ws.append(["nombre", "precio"])
    ws.append(["Lapiz", 2])
    ws.append(["Cuaderno", 15])
    ws.append(["Mochila", 120])
    ws.append(["Calculadora", 80])
    ws.append(["Regla", 5])
    ws.append(["Borrador", 1])

    out = Path(__file__).with_name("productos.xlsx")
    wb.save(out)
    print(f"OK: {out}")


if __name__ == "__main__":
    main()
