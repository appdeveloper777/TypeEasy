m = {"alpha":10,"beta":20,"gamma":30,"delta":40,"epsilon":50,"zeta":60,"eta":70,"theta":80,"iota":90,"kappa":100}
total = 0
for i in range(10000000):
    total += m["epsilon"]
print(total)
