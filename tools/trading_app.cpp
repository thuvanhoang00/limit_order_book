#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "order_book/trading/client.hpp"
#include "trading_args.hpp"

namespace {
using namespace order_book;
using namespace order_book::trading;
using namespace std::chrono_literals;
std::uint64_t number(const std::string& s,
                     std::uint64_t max = std::numeric_limits<std::uint64_t>::max()) {
    std::uint64_t n{};
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc{} || end != s.data() + s.size() || n > max)
        throw std::invalid_argument("Invalid script number: " + s);
    return n;
}
std::vector<std::string> fields(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream row(line);
    std::string field;
    std::vector<std::string> result;
    while (std::getline(row, field, ',')) result.push_back(field);
    return result;
}
void script(TradingClient& client, const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open orders script");
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#' || line.starts_with("action,")) continue;
        const auto f = fields(line);
        if (f.size() == 2 && f[0] == "WAIT") {
            client.poll(std::chrono::milliseconds(number(f[1], 60'000)));
            continue;
        }
        if (f.size() == 1 && f[0] == "SNAPSHOT") {
            client.recover();
            continue;
        }
        if (f.size() != 9)
            throw std::invalid_argument("Orders require nine comma-separated fields");
        Message c;
        if (f[0] == "NEW")
            c.action = Action::New;
        else if (f[0] == "CANCEL")
            c.action = Action::Cancel;
        else if (f[0] == "REPLACE")
            c.action = Action::Replace;
        else if (f[0] == "CANCEL_ALL")
            c.action = Action::CancelAll;
        else
            throw std::invalid_argument("Unknown order action");
        c.instrument_id = number(f[1]);
        c.order_id = number(f[2]);
        if (f[3] != "BUY" && f[3] != "SELL") throw std::invalid_argument("Expected BUY or SELL");
        c.side = f[3] == "BUY" ? Side::Bid : Side::Ask;
        if (f[4] != "LIMIT" && f[4] != "MARKET")
            throw std::invalid_argument("Expected LIMIT or MARKET");
        c.order_type = f[4] == "LIMIT" ? OrderType::Limit : OrderType::Market;
        if (f[5] == "GTC")
            c.tif = TimeInForce::GTC;
        else if (f[5] == "IOC")
            c.tif = TimeInForce::IOC;
        else if (f[5] == "FOK")
            c.tif = TimeInForce::FOK;
        else
            throw std::invalid_argument("Expected GTC, IOC or FOK");
        c.price = static_cast<Price>(number(f[6], std::numeric_limits<Price>::max()));
        c.quantity = static_cast<Quantity>(number(f[7], std::numeric_limits<Quantity>::max()));
        c.post_only = number(f[8], 1) != 0;
        (void)client.submit(c);
    }
    if (!input.eof()) throw std::runtime_error("Orders script read failed");
}
void strategy(TradingClient& client, const std::string& name, const StrategyConfig& cfg,
              std::uint64_t duration, ClientId id) {
    using Clock = std::chrono::steady_clock;
    const auto until = Clock::now() + std::chrono::milliseconds(duration);
    OrderId next_id{1};
    std::mt19937 random(id);
    while (Clock::now() < until) {
        client.poll(20ms);
        const auto inventory = client.orders().position(cfg.instrument).quantity;
        if (name == "maker") {
            const auto desired = maker_quotes(cfg, inventory);
            for (const auto side : {Side::Bid, Side::Ask}) {
                const auto wanted = std::find_if(desired.begin(), desired.end(),
                                                 [&](const auto& q) { return q.side == side; });
                auto current = std::find_if(client.orders().orders().begin(),
                                            client.orders().orders().end(), [&](const auto& p) {
                                                return p.second.instrument_id == cfg.instrument &&
                                                       p.second.side == side;
                                            });
                Message c;
                c.instrument_id = cfg.instrument;
                c.side = side;
                c.post_only = true;
                if (wanted == desired.end()) {
                    if (current == client.orders().orders().end()) continue;
                    c.action = Action::Cancel;
                    c.order_id = current->first;
                } else {
                    c.price = wanted->price;
                    c.quantity = wanted->quantity;
                    if (current == client.orders().orders().end())
                        c.order_id = next_id++;
                    else {
                        if (current->second.price == c.price &&
                            current->second.quantity == c.quantity)
                            continue;
                        c.action = Action::Replace;
                        c.order_id = current->first;
                    }
                }
                (void)client.submit(c);
            }
        } else if (name == "taker") {
            if (const auto q = taker_quote(cfg, client.market().book(cfg.instrument), inventory)) {
                Message c;
                c.instrument_id = cfg.instrument;
                c.order_id = next_id++;
                c.side = q->side;
                c.price = q->price;
                c.quantity = q->quantity;
                c.tif = TimeInForce::IOC;
                (void)client.submit(c);
            }
        } else if (name == "random") {
            const auto side = random() % 2 == 0 ? Side::Bid : Side::Ask;
            const auto signed_clip = side == Side::Bid ? static_cast<std::int64_t>(cfg.clip)
                                                       : -static_cast<std::int64_t>(cfg.clip);
            if (std::abs(inventory + signed_clip) <= cfg.inventory_limit) {
                Message c;
                c.instrument_id = cfg.instrument;
                c.order_id = next_id++;
                c.side = side;
                c.price = cfg.fair_price;
                c.quantity = cfg.clip;
                c.tif = TimeInForce::IOC;
                (void)client.submit(c);
            }
        } else
            throw std::invalid_argument("Strategy must be maker, taker or random");
    }
    Message cancel;
    cancel.action = Action::CancelAll;
    (void)client.submit(cancel);
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout
            << "trading_app --client-id N [--orders script.csv | --strategy maker|taker|random] "
               "[--tcp-port 9100] [--md-port 9101] [--group 239.255.0.2] [--interface 127.0.0.1] "
               "[--duration-ms 3000] [--instrument 1] [--fair-price 100] [--clip 5] [--edge 2] "
               "[--max-position 50] [--max-order-qty 100000] [--max-notional 10000000000] "
               "[--max-loss 100000]\n";
        return 0;
    }
    try {
        TradingArgs args(argc, argv);
        ClientConfig c;
        StrategyConfig s;
        c.client_id = static_cast<ClientId>(
            args.number("--client-id", 1, 1, std::numeric_limits<ClientId>::max()));
        c.tcp_port = static_cast<std::uint16_t>(args.number("--tcp-port", 9100, 1, 65535));
        c.multicast_port = static_cast<std::uint16_t>(args.number("--md-port", 9101, 1, 65535));
        c.group = args.text("--group", c.group);
        c.interface = args.text("--interface", c.interface);
        const auto orders = args.text("--orders", "");
        const auto algo = args.text("--strategy", "maker");
        const auto duration = args.number("--duration-ms", 3000, 1, 3'600'000);
        s.instrument = args.number("--instrument", 1, 1, std::numeric_limits<InstrumentId>::max());
        s.fair_price = static_cast<Price>(
            args.number("--fair-price", 100, 1, std::numeric_limits<Price>::max()));
        s.clip = static_cast<Quantity>(args.number("--clip", 5, 1, 100'000));
        s.edge = static_cast<Price>(args.number("--edge", 2, 1, 1'000'000));
        s.tick = static_cast<Price>(args.number("--tick", 1, 1, 1'000'000));
        s.inventory_limit =
            static_cast<std::int64_t>(args.number("--max-position", 50, 1, 1'000'000'000));
        c.limits.max_position = s.inventory_limit;
        c.limits.max_order_quantity =
            static_cast<Quantity>(args.number("--max-order-qty", 100'000, 1, 100'000'000));
        c.limits.max_notional = args.number("--max-notional", 10'000'000'000ULL, 1,
                                            std::numeric_limits<std::uint64_t>::max());
        c.max_loss = args.number("--max-loss", 100'000, 0, 1'000'000'000);
        args.finish();
        TradingClient client(c);
        client.set_report_handler([](const Message& m) {
            std::cout << "{\"report\":\"" << to_string(m.report) << "\",\"reject\":\""
                      << to_string(m.reject) << "\",\"request\":" << m.request_id
                      << ",\"order\":" << m.order_id << ",\"instrument\":" << m.instrument_id
                      << ",\"price\":" << m.price << ",\"quantity\":" << m.quantity
                      << ",\"remaining\":" << m.remaining << ",\"trade\":" << m.trade_id << "}"
                      << std::endl;
        });
        std::cout << "CLIENT_READY id=" << c.client_id << std::endl;
        if (!orders.empty())
            script(client, orders);
        else
            strategy(client, algo, s, duration, c.client_id);
        client.poll(100ms);
        client.recover();
        const auto p = client.orders().position(s.instrument);
        const auto* book = client.market().book(s.instrument);
        const long double mark =
            book && book->best_bid() && book->best_ask()
                ? (static_cast<long double>(*book->best_bid()) + *book->best_ask()) / 2
                : s.fair_price;
        std::cout << std::setprecision(18) << "{\"client\":" << c.client_id
                  << ",\"position\":" << p.quantity << ",\"cash\":" << p.cash
                  << ",\"realized_pnl\":" << p.realized_pnl << ",\"pnl\":" << p.pnl(mark)
                  << ",\"volume\":" << p.volume
                  << ",\"open_orders\":" << client.orders().orders().size()
                  << ",\"book_orders\":" << client.market().order_count()
                  << ",\"market_sequence\":" << client.market().sequence()
                  << ",\"recoveries\":" << client.recoveries() << ",\"synchronized\":true}"
                  << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Trading app: " << e.what() << '\n';
        return 1;
    }
}
