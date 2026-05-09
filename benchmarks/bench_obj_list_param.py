class Punto:
    __slots__ = ("x", "y")
    def __init__(self, x, y):
        self.x = x
        self.y = y

import sys
N = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
arr = []
for i in range(N):
    arr.append(Punto(i, i))
total = 0
for p in arr:
    total += p.x
print(total)
