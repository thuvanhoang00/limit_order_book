#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Extracted from the supplied Claude implementation; allocation algorithms
// are unchanged. Hardware/compiler claims in the original comments below
// are the author's claims, not results measured by this integration.
//
// FixedSlotPoolTungSahur / AllocatorAdaptorTungSahur are Claude's fallback
// reference implementation, not the user's FixedSlotPool / AllocatorAdaptor.
// The new competing allocator is SlotAllocator backed by SlotArena.
// No Payload64 definition, benchmark registration, or main belongs here.

template <std::size_t Bytes>
class FixedSlotPoolTungSahur {
public:
    FixedSlotPoolTungSahur() = default;
    FixedSlotPoolTungSahur(const FixedSlotPoolTungSahur&) = delete;
    FixedSlotPoolTungSahur& operator=(const FixedSlotPoolTungSahur&) = delete;

    void* allocate(std::size_t size, std::size_t align) {
        if (slot_size_ == 0) configure(size, align);          // kích thước slot chốt ở lần cấp đầu
        if (free_ != nullptr) {                                // ưu tiên slot đã được trả lại
            Slot* s = free_;
            free_ = s->next;
            return s;
        }
        if (static_cast<std::size_t>(buffer_ + Bytes - bump_) >= slot_size_) {
            void* p = bump_;                                   // chưa có slot trả lại: cắt slot mới
            bump_ += slot_size_;
            return p;
        }
        throw std::bad_alloc();
    }

    void deallocate(void* p) noexcept {
        Slot* s = static_cast<Slot*>(p);
        s->next = free_;
        free_ = s;
    }

private:
    struct Slot { Slot* next; };

    void configure(std::size_t size, std::size_t align) {
        const std::size_t a = align < alignof(Slot) ? alignof(Slot) : align;
        const std::size_t s = size < sizeof(Slot) ? sizeof(Slot) : size;
        slot_size_ = (s + a - 1) / a * a;
    }

    alignas(std::max_align_t) std::byte buffer_[Bytes];
    Slot* free_ = nullptr;
    std::byte* bump_ = buffer_;
    std::size_t slot_size_ = 0;
};

template <class T, std::size_t Bytes>
class AllocatorAdaptorTungSahur {
public:
    using value_type = T;
    template <class U> struct rebind { using other = AllocatorAdaptorTungSahur<U, Bytes>; };

    explicit AllocatorAdaptorTungSahur(FixedSlotPoolTungSahur<Bytes>& pool) noexcept : pool_(&pool) {}
    template <class U>
    AllocatorAdaptorTungSahur(const AllocatorAdaptorTungSahur<U, Bytes>& other) noexcept : pool_(other.pool()) {}

    T* allocate(std::size_t n) {
        if (n != 1) throw std::bad_alloc();
        return static_cast<T*>(pool_->allocate(sizeof(T), alignof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept { pool_->deallocate(p); }

    FixedSlotPoolTungSahur<Bytes>* pool() const noexcept { return pool_; }

    template <class U>
    bool operator==(const AllocatorAdaptorTungSahur<U, Bytes>& o) const noexcept { return pool_ == o.pool(); }
    template <class U>
    bool operator!=(const AllocatorAdaptorTungSahur<U, Bytes>& o) const noexcept { return pool_ != o.pool(); }

private:
    FixedSlotPoolTungSahur<Bytes>* pool_;
};

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PHẦN 1 — Macro tiện ích và công tắc cấu hình
// ══════════════════════════════════════════════════════════════════════════════════════════════
// SLOT_LIKELY / SLOT_UNLIKELY : báo nhánh nào là đường nóng để trình biên dịch xếp đường nóng
//                               thành mã thẳng (không có lệnh nhảy được thực hiện).
// SLOT_INLINE                 : ép inline. Không chỉ để nhanh: phép kiểm tra lúc biên dịch ở
//                               SlotPtr::operator= (PHẦN 3) chỉ có nghĩa khi hàm đã được inline
//                               vào đúng chỗ gọi.
// SLOT_COLD                   : đường hiếm (hết arena) bị đẩy ra khỏi vòng lặp nóng.
// SLOT_SPECULATE              : 1 nếu trình biên dịch có asm kiểu GNU (GCC/Clang). Trình biên
//                               dịch khác vẫn chạy đúng, chỉ mất mẹo ở mục (c).
#if defined(__GNUC__) || defined(__clang__)
#  define SLOT_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define SLOT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define SLOT_INLINE      inline __attribute__((always_inline))
#  define SLOT_COLD        __attribute__((noinline, cold))
#  define SLOT_SPECULATE   1
#else
#  define SLOT_LIKELY(x)   (x)
#  define SLOT_UNLIKELY(x) (x)
#  define SLOT_INLINE      inline
#  define SLOT_COLD
#  define SLOT_SPECULATE   0
#endif

// SLOT_FANCY_POINTERS: allocator trả về SlotPtr<T> (1) hay con trỏ thô T* (0).
//  - std::list của libstdc++ chỉ thật sự dùng kiểu con trỏ của allocator từ GCC 15 (macro
//    _GLIBCXX_USE_ALLOC_PTR_FOR_LIST); bản cũ hơn tự động rơi về T*.
//  - CẢNH BÁO: libstdc++ đi kèm GCC 16.2.1 (bản trên máy này; nhiều khả năng cả bản 15, nơi
//    đoạn mã đó được thêm vào) có sẵn một lỗi trong std::__list::_Node_base<Ptr>::swap (biến
//    __py bị tính từ __x thay vì __y). Hậu quả: std::list::swap() và std::list::sort() làm
//    hỏng danh sách với MỌI allocator dùng fancy pointer — không riêng gì allocator này.
//    Benchmark không đụng tới hai hàm đó; nếu mày cần chúng, biên dịch với
//    -DSLOT_FANCY_POINTERS=0. Chế độ đó đúng với mọi thao tác của list nhưng mất (b) và (c),
//    tốc độ chỉ còn ngang pool free-list.
#ifndef SLOT_FANCY_POINTERS
#  if defined(__GLIBCXX__) && !(defined(_GLIBCXX_USE_ALLOC_PTR_FOR_LIST) && _GLIBCXX_USE_ALLOC_PTR_FOR_LIST)
#    define SLOT_FANCY_POINTERS 0
#  else
#    define SLOT_FANCY_POINTERS 1
#  endif
#endif

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PHẦN 2 — slot_opaque(): "che mắt" trình biên dịch
// ══════════════════════════════════════════════════════════════════════════════════════════════
// Trả về đúng giá trị v, nhưng đi qua một lệnh asm rỗng nên trình biên dịch không còn biết kết
// quả bằng với cái gì. Không sinh ra lệnh máy nào ngoài (nhiều nhất) một lệnh mov giữa hai
// thanh ghi. Dùng ở PHẦN 3 để trình biên dịch không "tối ưu" mất phép đoán.
template <class V>
SLOT_INLINE V slot_opaque(V v) noexcept {
#if SLOT_SPECULATE
    asm("" : "+r"(v));
#endif
    return v;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PHẦN 3 — SlotPtr<T, Stride>: fancy pointer
// ══════════════════════════════════════════════════════════════════════════════════════════════
// Bên trong chỉ là một T* (8 byte), hành xử y như con trỏ thô. Nó tồn tại vì hai lý do:
//
//  1. Khi allocator::pointer không phải T*, libstdc++ dùng bộ node std::__list::_Node_base<Ptr>
//     viết hoàn toàn bằng template => _M_hook/_M_unhook được inline vào emplace_back (mục (b)).
//
//  2. std::list lưu con trỏ này trong chính các node (_M_next/_M_prev), và mọi phép duyệt
//     (clear(), ++iterator) đều là phép gán  cur = cur->_M_next  — tức là gọi operator= của
//     SlotPtr. Nhờ vậy có thể cài "value speculation" vào đúng phép gán đó (mục (c)).
//
// Stride = khoảng cách (byte) giữa hai slot liên tiếp của arena, cũng chính là khoảng cách giữa
// hai node được cấp liên tiếp.
template <class T, std::size_t Stride>
class SlotPtr {
public:
    // Ba khai báo để std::pointer_traits hiểu kiểu này (rebind sang kiểu khác vẫn giữ Stride).
    using element_type = T;
    using difference_type = std::ptrdiff_t;
    template <class U> using rebind = SlotPtr<U, Stride>;

    // Khởi tạo: mặc định (không gán gì, giống con trỏ thô), từ nullptr, từ con trỏ thô (explicit).
    SlotPtr() = default;
    constexpr SlotPtr(std::nullptr_t) noexcept : p_(nullptr) {}
    constexpr explicit SlotPtr(T* p) noexcept : p_(p) {}

    // Chuyển kiểu ngầm khi con trỏ thô cho phép (Node* -> NodeBase*, T* -> void*) ...
    template <class U, std::enable_if_t<std::is_convertible<U*, T*>::value && !std::is_same<U, T>::value, int> = 0>
    constexpr SlotPtr(const SlotPtr<U, Stride>& o) noexcept : p_(o.get()) {}
    // ... và chuyển ngược lại phải viết tường minh, tương đương static_cast (void* -> T*, Base* -> Derived*).
    template <class U, std::enable_if_t<std::is_convertible<T*, U*>::value && !std::is_convertible<U*, T*>::value, int> = 0>
    constexpr explicit SlotPtr(const SlotPtr<U, Stride>& o) noexcept : p_(static_cast<T*>(o.get())) {}

    SlotPtr(const SlotPtr&) = default;   // copy-construct là copy 8 byte bình thường

    SlotPtr& operator=(std::nullptr_t) noexcept {
        p_ = nullptr;
        return *this;
    }

    // Phép gán — nơi đặt value speculation.
    //
    // `hop` đúng khi vế phải (rhs) chính là 8 byte đầu tiên của object mà con trỏ này đang trỏ
    // tới, tức phép gán có dạng  p = p->next  với `next` là trường đầu tiên của node — đúng hình
    // dạng của mọi bước duyệt list trong libstdc++.
    //
    // __builtin_constant_p(hop) && hop : chỉ sinh mã speculation khi trình biên dịch CHỨNG MINH
    // được hop == true ngay lúc biên dịch (sau khi inline). Mọi phép gán khác (nối node trong
    // _M_hook, gán biến tạm, ...) không chứng minh được nên vẫn là một lệnh mov trần. Biểu thức
    // nằm trong __builtin_constant_p không được thực thi, nên p_ chưa khởi tạo cũng không sao.
    //
    // Phần speculation:
    //   here         = địa chỉ của chính trường `next` đang đọc = địa chỉ node hiện tại
    //   here + Stride = địa chỉ node nằm ở slot ngay sau trong arena (phép đoán)
    // Nếu giá trị vừa load (next) đúng bằng phép đoán thì trả về phép đoán được TÍNH LẠI từ địa
    // chỉ (qua slot_opaque) thay vì giá trị vừa load. Hai giá trị bằng nhau nên ngữ nghĩa không
    // đổi một bit nào; khác biệt nằm ở CPU: địa chỉ node kế giờ là kết quả của một phép cộng
    // (1 chu kỳ) chứ không phải của một lần load (5 chu kỳ), còn lần load chỉ dùng để kiểm chứng
    // qua bộ dự đoán nhánh. Chuỗi phụ thuộc 5 chu kỳ/node của clear() biến mất.
    // Đoán sai (list bị xáo thứ tự, node cuối trỏ về sentinel, ...) => nhánh không được chọn,
    // dùng giá trị load như thường; cái giá chỉ là một lần dự đoán nhánh sai.
    //
    // slot_opaque là bắt buộc: thiếu nó, trình biên dịch thấy "nếu next == A thì next = A" là
    // câu lệnh vô nghĩa và xoá luôn phép đoán.
    SLOT_INLINE SlotPtr& operator=(const SlotPtr& rhs) noexcept {
        T* next = rhs.p_;
#if SLOT_SPECULATE
        const bool hop = static_cast<const void*>(std::addressof(rhs)) == static_cast<const void*>(p_);
        if (__builtin_constant_p(hop) && hop) {
            const std::uintptr_t here = reinterpret_cast<std::uintptr_t>(std::addressof(rhs));
            if (SLOT_LIKELY(reinterpret_cast<std::uintptr_t>(next) == here + Stride))
                next = reinterpret_cast<T*>(slot_opaque(here) + Stride);
        }
#endif
        p_ = next;
        return *this;
    }

    // std::pointer_traits::pointer_to: container dùng để lấy "con trỏ tới chính node này".
    template <class U = T, std::enable_if_t<!std::is_void<U>::value, int> = 0>
    static SlotPtr pointer_to(U& r) noexcept { return SlotPtr(std::addressof(r)); }

    // Phần còn lại là giao diện tối thiểu của một con trỏ.
    constexpr T* get() const noexcept { return p_; }
    template <class U = T, std::enable_if_t<!std::is_void<U>::value, int> = 0>
    constexpr U& operator*() const noexcept { return *p_; }
    constexpr T* operator->() const noexcept { return p_; }
    constexpr explicit operator bool() const noexcept { return p_ != nullptr; }
    friend constexpr bool operator==(const SlotPtr& a, const SlotPtr& b) noexcept { return a.p_ == b.p_; }
    friend constexpr bool operator!=(const SlotPtr& a, const SlotPtr& b) noexcept { return a.p_ != b.p_; }

private:
    T* p_;
};

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PHẦN 4 — SlotArena<SlotBytes, PoolBytes>: nơi chứa bộ nhớ
// ══════════════════════════════════════════════════════════════════════════════════════════════
// Bộ nhớ được chia thành các slot SlotBytes byte (bội số của 64 = một cache line), căn theo
// cache line. Ba ý chính:
//
//  • Cấp phát = cộng con trỏ (bump): lấy cur_, tăng cur_ thêm một slot. Không đọc gì từ node,
//    nên không có chuỗi phụ thuộc qua bộ nhớ như phép "pop" của free-list.
//
//  • Trả lại = giảm bộ đếm live_. Khi live_ về 0 (mọi node đã được trả, theo bất kỳ thứ tự nào)
//    thì cur_ quay về đầu buffer: lượt kế tiếp dùng lại đúng những slot vừa dùng, vẫn còn nóng
//    trong L1, và vẫn xếp tăng dần nên phép đoán ở PHẦN 3 luôn trúng.
//    give() không gọi hàm nào và không ghi qua con trỏ lạ, nên trong vòng lặp clear() GCC giữ
//    live_ trong thanh ghi suốt vòng lặp: mỗi node chỉ tốn một lệnh trừ + một nhánh (Clang để
//    live_ trong bộ nhớ, chậm hơn GCC một chút nhưng vẫn thắng rõ).
//    (Tao đã thử phương án ghi nhận từng slot được trả — nó thêm một cặp store→load qua bộ nhớ
//    ở mỗi node và đo thấy vừa chậm hơn vừa kém ổn định.)
//
//  • Hết buffer mà list vẫn chưa rỗng: lấy thêm cả một khối từ heap rồi bump tiếp trong khối
//    đó. Các khối được giữ lại để dùng cho lần sau và chỉ trả về heap khi arena bị huỷ.
//
// GIỚI HẠN CẦN BIẾT (đánh đổi lấy tốc độ):
//  - Đây là arena theo "lượt": bộ nhớ chỉ được tái sử dụng khi tất cả node đã được trả. Một
//    list không bao giờ rỗng (ví dụ hàng đợi sống mãi, push/pop xen kẽ) sẽ làm arena lớn dần.
//    Kiểu dùng đó hợp với pool free-list hơn.
//  - Mỗi node chiếm trọn SlotBytes (128 byte cho node 80 byte): đổi bộ nhớ lấy tốc độ.
//  - Không an toàn đa luồng (giống FixedSlotPool), và arena phải sống lâu hơn mọi container
//    đang dùng nó.
template <std::size_t SlotBytes, std::size_t PoolBytes>
class SlotArena {
public:
    static constexpr std::size_t kLine  = 64;                       // kích thước cache line
    static constexpr std::size_t kSlots = PoolBytes / SlotBytes;    // số slot trong buffer gắn liền
    static_assert(SlotBytes % kLine == 0 && kSlots > 0, "SlotBytes phai la boi so cua 64 va vua trong pool");

    SlotArena() noexcept
        : cur_(buf_), end_(buf_ + kSlots * SlotBytes), live_(0), block_(nullptr), blocks_(nullptr) {}
    SlotArena(const SlotArena&) = delete;
    SlotArena& operator=(const SlotArena&) = delete;

    ~SlotArena() {                                  // trả các khối mở rộng (nếu có) về heap
        while (blocks_ != nullptr) {
            Block* next = blocks_->next;
            ::operator delete(blocks_, std::align_val_t(kLine));
            blocks_ = next;
        }
    }

    // Đường nóng của allocate: đọc cur_, so với end_, rồi 2 store (cur_ và live_). Hai trường
    // này nằm cạnh nhau trong cùng một cache line nên store buffer ghi cả hai trong một chu kỳ.
    SLOT_INLINE char* take() {
        char* s = cur_;
        if (SLOT_UNLIKELY(s == end_)) return take_slow();
        cur_ = s + SlotBytes;
        ++live_;
        return s;
    }

    // Đường nóng của deallocate. Không cần biết slot nào được trả: chỉ cần biết còn bao nhiêu.
    SLOT_INLINE void give() noexcept {
        if (SLOT_UNLIKELY(--live_ == 0)) {          // node cuối cùng vừa được trả: quay về đầu
            cur_ = buf_;
            end_ = buf_ + kSlots * SlotBytes;
            block_ = nullptr;
        }
    }

private:
    struct Block { Block* next; };                  // đầu mỗi khối mở rộng; các slot bắt đầu sau 64 byte

    // Đường hiếm: khối hiện tại đã dùng hết. Chuyển sang khối kế (đã có từ lượt trước) hoặc xin
    // khối mới. operator new được gọi trước khi sửa trạng thái nên nếu nó ném bad_alloc thì
    // arena vẫn nguyên vẹn.
    SLOT_COLD char* take_slow() {
        Block* b = block_ != nullptr ? block_->next : blocks_;
        if (b == nullptr) {
            b = static_cast<Block*>(::operator new(kLine + kSlots * SlotBytes, std::align_val_t(kLine)));
            b->next = nullptr;
            (block_ != nullptr ? block_->next : blocks_) = b;
        }
        block_ = b;
        char* s = reinterpret_cast<char*>(b) + kLine;
        end_ = s + kSlots * SlotBytes;
        cur_ = s + SlotBytes;
        ++live_;
        return s;
    }

    char* cur_;            // slot sẽ được cấp ở lần gọi kế tiếp
    char* end_;            // hết khối hiện tại
    std::size_t live_;     // số node đang được container giữ
    Block* block_;         // khối mở rộng đang dùng (nullptr = đang ở buffer gắn liền)
    Block* blocks_;        // danh sách mọi khối mở rộng đã xin
    alignas(kLine) char buf_[PoolBytes];   // buffer gắn liền; các trường điều khiển ở trên nằm trọn trong 1 cache line riêng
};

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PHẦN 5 — SlotAllocator<T, SlotBytes, PoolBytes>: lớp vỏ theo đúng giao diện Allocator
// ══════════════════════════════════════════════════════════════════════════════════════════════
// Giữ một con trỏ tới arena (giống AllocatorAdaptor giữ con trỏ tới pool); hai allocator bằng
// nhau khi dùng chung arena. std::list không cấp phát T mà cấp phát kiểu node nội bộ của nó
// thông qua rebind<Node>::other, nên mọi tính toán kích thước bên dưới là theo kiểu đã rebind.
template <class T, std::size_t SlotBytes, std::size_t PoolBytes>
class SlotAllocator {
public:
    using Arena = SlotArena<SlotBytes, PoolBytes>;
    using value_type = T;
#if SLOT_FANCY_POINTERS
    using pointer = SlotPtr<T, SlotBytes>;          // bật (b) và (c)
#else
    using pointer = T*;
#endif
    template <class U> struct rebind { using other = SlotAllocator<U, SlotBytes, PoolBytes>; };

    explicit SlotAllocator(Arena& arena) noexcept : arena_(&arena) {}
    template <class U>
    SlotAllocator(const SlotAllocator<U, SlotBytes, PoolBytes>& o) noexcept : arena_(o.arena()) {}

    // Node được đặt ở CUỐI slot (kHead byte đệm ở đầu). Với node của std::list<Payload64>
    // (16 byte liên kết + 64 byte payload = 80 byte) trong slot 128 byte:
    //     [ 48 byte đệm | 16 byte next/prev ]  [ 64 byte payload ]
    //       └────────── cache line 1 ───────┘  └── cache line 2 ──┘
    // Payload chiếm trọn một cache line: mọi store xoá payload rơi vào cùng một line (được ghi
    // xuống theo cặp, và không store nào bị cắt đôi qua ranh giới line — với cách đặt node sát
    // nhau thông thường, GCC -O2 sinh ra store 16 byte vắt qua hai line ở 3/4 số node). Hai
    // store next/prev cũng nằm chung một line. Thứ tự phát store là việc của trình biên dịch;
    // bố cục này chỉ bảo đảm rằng thứ tự nào thì chúng cũng chỉ chạm tới 2 cache line.
    SLOT_INLINE pointer allocate(std::size_t n) {
        static_assert(kFits, "SlotAllocator: kieu node khong vua mot slot - hay tang SlotBytes");
        if (SLOT_LIKELY(n == 1)) return pointer(reinterpret_cast<T*>(arena_->take() + kHead));
        // Container dạng node luôn xin đúng 1 node; n != 1 (mảng) thì đi thẳng xuống heap.
        return pointer(static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T)))));
    }

    SLOT_INLINE void deallocate(pointer p, std::size_t n) noexcept {
        if (SLOT_LIKELY(n == 1)) {
            arena_->give();
            return;
        }
        ::operator delete(raw(p), std::align_val_t(alignof(T)));
    }

    Arena* arena() const noexcept { return arena_; }

    template <class U>
    bool operator==(const SlotAllocator<U, SlotBytes, PoolBytes>& o) const noexcept { return arena_ == o.arena(); }
    template <class U>
    bool operator!=(const SlotAllocator<U, SlotBytes, PoolBytes>& o) const noexcept { return arena_ != o.arena(); }

private:
    // Kiểu node phải vừa slot, không đòi căn chỉnh quá một cache line, và vị trí cuối slot phải
    // đúng căn chỉnh của nó. Sai thì báo lỗi ngay lúc biên dịch thay vì âm thầm chạy chậm.
    static constexpr bool kFits =
        sizeof(T) <= SlotBytes && alignof(T) <= Arena::kLine && (SlotBytes - sizeof(T)) % alignof(T) == 0;
    static constexpr std::size_t kHead = kFits ? SlotBytes - sizeof(T) : 0;

    static T* raw(T* p) noexcept { return p; }
    template <std::size_t S>
    static T* raw(const SlotPtr<T, S>& p) noexcept { return p.get(); }

    Arena* arena_;
};

// Kích thước slot cho node của std::list<T>: hai con trỏ liên kết, rồi T (đúng căn chỉnh của
// T), làm tròn lên bội số của cache line. Payload64 -> 16 + 64 = 80 -> 128.
template <class T>
constexpr std::size_t list_slot_bytes() noexcept {
    const std::size_t a = alignof(T) > alignof(void*) ? alignof(T) : alignof(void*);
    const std::size_t links = (2 * sizeof(void*) + a - 1) / a * a;
    const std::size_t node = (links + sizeof(T) + a - 1) / a * a;
    return (node + 63) / 64 * 64;
}

