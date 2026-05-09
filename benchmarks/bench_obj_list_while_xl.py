N = 100000
arr = []
class Punto:
    __slots__ = ("x","y")
    def __init__(self, x, y):
        self.x = x; self.y = y
i = 0
while i < N:
    arr.append(Punto(i, i))
    i += 1
total = 0
k = 0
while k < 100:
    j = 0
    while j < N:
        total = total + arr[j].x
        j += 1
    k += 1
print(total)
