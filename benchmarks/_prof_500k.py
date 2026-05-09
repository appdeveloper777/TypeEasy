class Punto:
    __slots__ = ('x','y')
    def __init__(s,x,y): s.x=x; s.y=y
arr=[]
for i in range(500000): arr.append(Punto(i,i))
total=0
for j in range(500000): total += arr[j].x
print(total)
