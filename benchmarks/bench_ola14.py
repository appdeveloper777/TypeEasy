m = {"one":1,"two":2,"three":3,"four":4,"five":5,"six":6,"seven":7,"eight":8}
xs = [10,20,30,40,50,60,70,80,90,100]
total = 0
for i in range(5000000):
    total += m["five"]
    total += xs[7]
print(total)
