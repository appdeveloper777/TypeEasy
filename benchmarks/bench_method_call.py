class Calc:
    def __init__(self, b):
        self.base = b
    def add(self, x):
        return self.base + x

c = Calc(7)
total = 0
for i in range(20000000):
    r = c.add(3)
    total = total + r
print(total)
