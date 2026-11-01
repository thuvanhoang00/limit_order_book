import argparse, gzip, hashlib, json, struct
from collections import Counter, OrderedDict
from pathlib import Path
parser = argparse.ArgumentParser(description='Independently verify the first 10M ITCH records against the archived AAPL FIFO snapshot.')
parser.add_argument('--input', type=Path, default=Path('data/01302019.NASDAQ_ITCH50.gz'))
parser.add_argument('--snapshot', type=Path, default=Path(__file__).with_name('snapshot.json'))
parser.add_argument('--output', type=Path, default=Path(__file__).with_name('verification.json'))
args = parser.parse_args()
source = args.input
limit = 10_000_000
orders = OrderedDict()
locate = None
counts = Counter()
digest = hashlib.sha256()
peak = 0
with gzip.open(source, 'rb') as stream:
    for index in range(limit):
        prefix = stream.read(2)
        assert len(prefix) == 2
        size = int.from_bytes(prefix, 'big')
        message = stream.read(size)
        assert len(message) == size
        digest.update(prefix)
        digest.update(message)
        kind = chr(message[0])
        code = int.from_bytes(message[1:3], 'big')
        if kind == 'R' and message[11:19].rstrip() == b'AAPL':
            locate = code
        if code != locate:
            continue
        counts[kind] += 1
        if kind in ('A', 'F'):
            oid, side, quantity, symbol, price = struct.unpack_from('!QcI8sI', message, 11)
            assert oid not in orders and quantity > 0
            orders[oid] = [side.decode(), price, quantity]
        elif kind in ('E', 'C', 'X'):
            oid, decrement = struct.unpack_from('!QI', message, 11)
            assert decrement <= orders[oid][2]
            orders[oid][2] -= decrement
            if orders[oid][2] == 0:
                del orders[oid]
        elif kind == 'D':
            oid = struct.unpack_from('!Q', message, 11)[0]
            del orders[oid]
        elif kind == 'U':
            oid, newid, quantity, price = struct.unpack_from('!QQII', message, 11)
            side = orders.pop(oid)[0]
            assert newid not in orders
            orders[newid] = [side, price, quantity]
        peak = max(peak, len(orders))
levels = {}
for oid, (side, price, quantity) in orders.items():
    level = levels.setdefault((side, price), {'side':'bid' if side == 'B' else 'ask',
        'price':price, 'quantity':0, 'orders':[]})
    level['quantity'] += quantity
    level['orders'].append({'id':oid,'quantity':quantity})
expected = sorted(levels.values(), key=lambda row: (row['side'] == 'ask',
    -row['price'] if row['side'] == 'bid' else row['price']))
actual = json.loads(args.snapshot.read_text())['books']
assert len(actual) == 1 and actual[0]['symbol'] == 'AAPL'
assert actual[0]['levels'] == expected, 'C++ snapshot differs from independent Python reconstruction'
result = dict(records=limit, symbol='AAPL', stock_locate=locate, final_orders=len(orders),
    peak_orders=peak, symbol_message_counts=counts, decoded_prefix_sha256=digest.hexdigest(),
    fifo_levels_and_quantities_match=True, complete_day=False)
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
