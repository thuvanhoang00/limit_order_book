# UDP multicast + LOB: test local

UDP multicast đã tích hợp cùng TCP ingress, codec và logger của LOB.
Linux, C++20; chạy loopback không cần hai máy hay quyền root.

## 1. Build từ thư mục chứa CMakeLists.txt

Chạy từng dòng, không gõ chữ `sh`/`bash` ở nhãn code block:

```sh
cmake -S . -B build/udp -DCMAKE_BUILD_TYPE=Debug -DORDER_BOOK_BUILD_BENCHMARKS=OFF
cmake --build build/udp --parallel
ctest --test-dir build/udp --output-on-failure
```

GoogleTest được tải khi configure lần đầu nếu máy chưa có. Có thể dùng
`-DORDER_BOOK_FETCH_DEPS=OFF` nếu đã cài dependency. Python 3 bật test nhiều process.
Build tools không cần GoogleTest: thêm `-DORDER_BOOK_BUILD_TESTS=OFF` khi configure.

## 2. Terminal 1: chạy subscriber trước

```sh
./build/udp/udp_event_subscriber 60
```

Đợi hiện `Joined group=239.255.0.1 port=9001 interface=127.0.0.1`.
Subscriber đợi tối đa 30 giây cho mỗi lần nhận; nếu hết hạn thì chạy lại.

## 3. Terminal 2: publisher gửi CSV hiện có

```sh
./build/udp/udp_event_publisher data/sample_events.csv
```

Publisher gửi 60 datagram, mỗi datagram chứa một event 40 byte, mặc định cách nhau
1 ms. Subscriber decode và apply vào `OrderBookRegistry` trên cùng thread, sau đó
thoát khi apply đủ 60 event. Nó in CSV event đã apply, số order/level cuối cùng và:

```text
book_synchronized=1 sequence=60 received=60 applied=60 stale=0
```

Registry ghi log bất đồng bộ vào `udp_subscriber.log`. TCP và replay vẫn dùng được.
Chạy lại subscriber tạo book trống; publisher kết thúc không tạo EOF cho UDP.

## Test một publisher, hai subscriber

Trước khi chạy publisher, mở thêm terminal với một subscriber cùng group/port:

```sh
./build/udp/udp_event_subscriber 60 9001 239.255.0.1 127.0.0.1 30000 udp_second.log
```

Đợi **cả hai** hiện `Joined`, rồi chạy publisher một lần. Hai subscriber đều nhận
luồng sự kiện và cập nhật book riêng. Dùng log_file riêng cho mỗi process.

## Test tự động UDP và các ca lỗi

```sh
ctest --test-dir build/udp -R '^Udp' --output-on-failure
python3 tests/net/test_udp_cli.py --bin-dir build/udp -v
```

Các test dùng multicast loopback thật: state khớp CSV replay, fan-out hai book,
duplicate/late-old packet, gap, join muộn, mất gói cuối (timeout), datagram sai
format/kích thước và book reject. Không dùng xác nhận từ publisher để kết luận
subscriber đã nhận.

## Chính sách của bản đầu

- Dùng lại `EventFrame` 40 byte của TCP, một event/datagram. Không có ACK.
- Stream bắt đầu tại sequence 1, tăng liên tiếp trên toàn bộ instrument.
- Sequence cũ/trùng: bỏ qua, không apply lại.
- Sequence nhảy tới tương lai: dừng trước khi apply, `book_synchronized=0`.
- Datagram lỗi hoặc book reject: dừng; chỉ apply thành công mới commit sequence.
- Chưa có reorder buffer, snapshot, retransmission hay recovery. Gap cần khôi phục
  từ nguồn tin cậy trước khi dùng tiếp book. Không reset sequence để bỏ qua gap.

Chi tiết API, lựa chọn socket option và giới hạn nằm trong
[docs/udp_multicast.md](docs/udp_multicast.md).

## Kiểm tra với sanitizer

```sh
./tools/run_tests.sh asan
```

Preset `asan` bật cả AddressSanitizer và UndefinedBehaviorSanitizer, chạy cùng bộ
test core, memory, TCP và UDP. Test CLI UDP có 10 ca, gồm nhiều process multicast
thật; 8 test GTest kiểm tra transport UDP. Chưa đo latency hoặc test LAN.
