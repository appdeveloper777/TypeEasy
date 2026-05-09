import sys
class Punto:
    __slots__ = ('x', 'y')
    def __init__(self, x, y):
        self.x = x
        self.y = y

N = int(sys.argv[1])
arr = []
for i in range(N):
    arr.append(Punto(i, i))
total = 0
for j in range(N):
    total += arr[j].x
print(total)
