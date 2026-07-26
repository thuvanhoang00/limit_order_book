# Data

Generated CSV format:

```text
sequence,type,side,order_id,price,quantity
1,A,B,1001,10000,25
2,E,,1001,0,5
3,C,,1001,0,0
```

Event types:

- `A`: add order
- `C`: cancel the remaining order quantity
- `E`: execute a quantity against an existing order

Generate a deterministic workload:

```bash
./build/release/generate_events data/events.csv 1000000 42
```
