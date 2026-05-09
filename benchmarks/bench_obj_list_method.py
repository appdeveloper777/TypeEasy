N = 100000
arr = []
class Punto:
    __slots__ = ("x","y")
    def __init__(self, x, y):
        self.x = x; self.y = y
class Sumador:
    def sum_passes(self, arr, n_items, n_passes):
        total = 0
        for k in range(n_passes):
            for j in range(n_items):
                total = total + arr[j].x
        return total
for i in range(N):
    arr.append(Punto(i, i))
s = Sumador()
print(s.sum_passes(arr, N, 100))
