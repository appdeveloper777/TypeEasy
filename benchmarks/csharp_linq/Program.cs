using System.Diagnostics;
using System.Globalization;

var swLoad = Stopwatch.StartNew();
var productos = new List<(int id, string nombre, int precio, string categoria)>(11_000_000);
using (var sr = new StreamReader("benchmarks/bench_10m.csv"))
{
    sr.ReadLine(); // header
    string? line;
    while ((line = sr.ReadLine()) != null)
    {
        // id,nombre,precio,categoria
        int c1 = line.IndexOf(',');
        int c2 = line.IndexOf(',', c1 + 1);
        int c3 = line.IndexOf(',', c2 + 1);
        int id = int.Parse(line.AsSpan(0, c1));
        string nombre = line.Substring(c1 + 1, c2 - c1 - 1);
        int precio = int.Parse(line.AsSpan(c2 + 1, c3 - c2 - 1));
        string categoria = line.Substring(c3 + 1);
        productos.Add((id, nombre, precio, categoria));
    }
}
swLoad.Stop();

var swQuery = Stopwatch.StartNew();
var dulces = productos.Where(p => p.categoria == "dulce").ToList();
long cnt = dulces.Count;
long suma = dulces.Sum(p => (long)p.precio);
double prom = dulces.Average(p => (double)p.precio);
swQuery.Stop();

Console.WriteLine($"count={cnt} sum={suma} avg={prom.ToString("F6", CultureInfo.InvariantCulture)}");
Console.WriteLine($"[csharp] load={swLoad.ElapsedMilliseconds}ms query={swQuery.ElapsedMilliseconds}ms total={swLoad.ElapsedMilliseconds + swQuery.ElapsedMilliseconds}ms");
