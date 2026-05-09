N = 50000
arr = []
class Punto:
    __slots__ = ("x","y")
    def __init__(self, x, y):
        self.x = x; self.y = y
for i in range(N):
    arr.append(Punto(i, i))
total = 0
for k in range(20):
    for j in range(N):
        total = total + arr[j].x
print(total)
