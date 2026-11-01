# Data

Native historical exchange archives can be replayed directly with
`replay_market_data`; see [the native feed guide](../docs/market_data.md).
Downloaded `.gz`, `.bin`, `.pitch` and partial browser downloads in this directory
are ignored by Git. Start Nasdaq ITCH replay at the beginning of the archive;
use `--symbol AAPL` to select a book without losing its earlier state.

Generated CSV format (older replay/generator tools):

```text
sequence,instrument_id,event_type,order_id,side,price,quantity
1,1001,ADD,1001,BID,10000,25
2,1001,EXECUTE,1001,,,5
3,1001,CANCEL,1001,,,
```

Columns:

- `sequence`: global, strictly contiguous event sequence starting at `1`
- `instrument_id`: numeric identifier used to route the event to an order book
- `event_type`: `ADD`, `CANCEL`, or `EXECUTE`
- `order_id`: identifier of the affected order
- `side`: `BID` or `ASK`; present only for `ADD`
- `price`: integer price in the instrument's smallest price unit; present only for `ADD`
- `quantity`: order quantity for `ADD`, executed quantity for `EXECUTE`, empty for `CANCEL`

Event types:

- `ADD`: add order; `side`, `price`, and `quantity` are required
- `CANCEL`: cancel the remaining order quantity; non-applicable fields are empty
- `EXECUTE`: execute a quantity against an existing order; `quantity` is required

Generate a deterministic workload:

```bash
./build/release/generate_events data/events.csv 1000000 42 1001
```

The final argument is the instrument ID and defaults to `1` when omitted.
