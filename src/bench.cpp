#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "order_book.hpp"

using namespace me;
using Clock = std::chrono::high_resolution_clock;
using ns64 = std::uint64_t;

static ns64 percentile_copy(std::vector<ns64> v, double p) {
    if (v.empty()) return 0;
    size_t k = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

struct Stat {
    std::vector<ns64> lat;
    void add(ns64 x) { lat.push_back(x); }

    void summary(const std::string &name) const {
        auto p50 = percentile_copy(lat, 0.50);
        auto p95 = percentile_copy(lat, 0.95);
        auto p99 = percentile_copy(lat, 0.99);
        std::cout << name << "  n=" << lat.size()
                << "  p50=" << p50 << "ns"
                << "  p95=" << p95 << "ns"
                << "  p99=" << p99 << "ns\n";
    }
};

struct IdGen {
    OrderId id{1};
    Ts ts{1};
    OrderId next_id() { return id++; }
    Ts next_ts() { return ts++; }
};

struct LiveSet {
    std::vector<OrderId> vec;
    std::unordered_set<OrderId> set;

    void add(OrderId id) {
        if (set.insert(id).second) vec.push_back(id);
    }

    bool erase(OrderId id) {
        if (!set.erase(id)) return false;
        return true;
    }

    template<class URNG>
    OrderId pick(URNG &rng) {
        while (!vec.empty()) {
            std::uniform_int_distribution<size_t> di(0, vec.size() - 1);
            size_t i = di(rng);
            OrderId cand = vec[i];
            if (set.count(cand)) return cand;
            // выкидываем «мертвого» из вектора
            vec[i] = vec.back();
            vec.pop_back();
        }
        return 0;
    }
};

struct Csv {
    std::ofstream f;

    explicit Csv(const std::string &path)
        : f(path, std::ios::out | std::ios::trunc) {
        f << "scenario,op,latency_ns\n";
    }

    void row(const std::string &scen, const std::string &op, ns64 ns) {
        f << scen << ',' << op << ',' << ns << '\n';
    }
};

struct Bench {
    OrderBook ob;
    IdGen gen;
    LiveSet live;
    std::mt19937_64 rng;

    std::unordered_map<std::string, Stat> S;

    explicit Bench(std::uint64_t seed) : rng(seed) {
    }

    void do_post(const std::string &scen, Side side, Price px, Qty qty, Csv &csv) {
        Order o{gen.next_id(), side, OrdType::Limit, px, qty, gen.next_ts()};
        auto t0 = Clock::now();
        ob.post_passive(o); // постим без матчинга
        auto t1 = Clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        S["post"].add((ns64) dur);
        csv.row(scen, "post", (ns64) dur);
        live.add(o.id);
    }

    void do_add_limit_cross(const std::string &scen, Side side, Price px, Qty qty, Csv &csv) {
        Order o{gen.next_id(), side, OrdType::Limit, px, qty, gen.next_ts()};
        auto t0 = Clock::now();
        auto trades = ob.add_limit(o);
        auto t1 = Clock::now();
        (void) trades;
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        S["add_limit"].add((ns64) dur);
        csv.row(scen, "add_limit", (ns64) dur);
    }

    void do_add_market(const std::string &scen, Side side, Qty qty, Csv &csv) {
        Order o{gen.next_id(), side, OrdType::Market, 0, qty, gen.next_ts()};
        auto t0 = Clock::now();
        auto trades = ob.add_market(o);
        auto t1 = Clock::now();
        (void) trades;
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        S["add_market"].add((ns64) dur);
        csv.row(scen, "add_market", (ns64) dur);
    }

    void do_cancel(const std::string &scen, Csv &csv) {
        OrderId id = live.pick(rng);
        if (id == 0) return;
        auto t0 = Clock::now();
        bool ok = ob.cancel(id);
        auto t1 = Clock::now();
        if (ok) live.erase(id);
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        S["cancel"].add((ns64) dur);
        csv.row(scen, "cancel", (ns64) dur);
    }

    void do_modify_down(const std::string &scen, Csv &csv) {
        OrderId id = live.pick(rng);
        if (id == 0) return;
        auto t0 = Clock::now();
        auto trades = ob.modify(id, std::nullopt, (Qty) 1, gen.next_ts());
        auto t1 = Clock::now();
        (void) trades;
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        S["modify"].add((ns64) dur);
        csv.row(scen, "modify", (ns64) dur);
    }

    void run_burst(std::size_t ops, Csv &csv) {
        const std::string scen = "burst";
        Price mid = 10000;
        std::uniform_int_distribution<int> qtyd(1, 50);
        std::uniform_int_distribution<int> sided(0, 1);
        std::uniform_int_distribution<int> offd(1, 5);

        for (int i = 0; i < 200; ++i) {
            Side s = (i & 1) ? Side::Buy : Side::Sell;
            Price px = (s == Side::Buy) ? mid - offd(rng) : mid + offd(rng);
            do_post(scen, s, px, qtyd(rng), csv);
        }

        auto t_all0 = Clock::now();
        for (std::size_t i = 1; i <= ops; ++i) {
            int r = (int) (i % 20);
            if (r < 14) {
                Side s = sided(rng) ? Side::Buy : Side::Sell;
                Price px;
                if (s == Side::Buy) {
                    auto a = ob.best_ask();
                    px = a
                             ? std::min<Price>(a->first - 1, mid - offd(rng))
                             : mid - offd(rng);
                } else {
                    auto b = ob.best_bid();
                    px = b
                             ? std::max<Price>(b->first + 1, mid + offd(rng))
                             : mid + offd(rng);
                }
                do_post(scen, s, px, qtyd(rng), csv);
            } else if (r < 17) {
                Side s = sided(rng) ? Side::Buy : Side::Sell;
                if (s == Side::Buy) {
                    auto a = ob.best_ask();
                    Price px = a ? a->first : (mid + 1);
                    do_add_limit_cross(scen, s, px, qtyd(rng), csv);
                } else {
                    auto b = ob.best_bid();
                    Price px = b ? b->first : (mid - 1);
                    do_add_limit_cross(scen, s, px, qtyd(rng), csv);
                }
            } else if (r < 19) {
                Side s = sided(rng) ? Side::Buy : Side::Sell;
                do_add_market(scen, s, qtyd(rng), csv);
            } else {
                if ((i & 1) == 0) do_cancel(scen, csv);
                else do_modify_down(scen, csv);
            }
        }
        auto t_all1 = Clock::now();
        double secs = std::chrono::duration<double>(t_all1 - t_all0).count();
        std::cout << "\n[burst] total_ops=" << ops << "  elapsed=" << secs
                << "s  throughput=" << (ops / secs) << " ops/s\n";
    }

    void run_poisson(std::size_t ops, Csv &csv) {
        const std::string scen = "poisson";
        Price mid = 10000;
        std::uniform_int_distribution<int> qtyd(1, 50);
        std::uniform_int_distribution<int> sided(0, 1);
        std::discrete_distribution<int> choice({30, 50, 10, 5, 5}); // post, add_lmt, market, cancel, modify
        std::uniform_int_distribution<int> offd(1, 8);

        for (int i = 0; i < 200; ++i) {
            Side s = (i & 1) ? Side::Buy : Side::Sell;
            Price px = (s == Side::Buy) ? mid - offd(rng) : mid + offd(rng);
            do_post(scen, s, px, qtyd(rng), csv);
        }

        auto t_all0 = Clock::now();
        for (std::size_t i = 0; i < ops; ++i) {
            int op = choice(rng);
            switch (op) {
                case 0: {
                    Side s = sided(rng) ? Side::Buy : Side::Sell;
                    Price px = (s == Side::Buy)
                                   ? (ob.best_ask()
                                          ? std::min<Price>(ob.best_ask()->first - 1, mid - offd(rng))
                                          : mid - offd(rng))
                                   : (ob.best_bid()
                                          ? std::max<Price>(ob.best_bid()->first + 1, mid + offd(rng))
                                          : mid + offd(rng));
                    do_post(scen, s, px, qtyd(rng), csv);
                    break;
                }
                case 1: {
                    Side s = sided(rng) ? Side::Buy : Side::Sell;
                    Price px = (s == Side::Buy)
                                   ? (ob.best_ask() ? ob.best_ask()->first : mid + 1)
                                   : (ob.best_bid() ? ob.best_bid()->first : mid - 1);
                    do_add_limit_cross(scen, s, px, qtyd(rng), csv);
                    break;
                }
                case 2: {
                    Side s = sided(rng) ? Side::Buy : Side::Sell;
                    do_add_market(scen, s, qtyd(rng), csv);
                    break;
                }
                case 3: {
                    do_cancel(scen, csv);
                    break;
                }
                case 4: {
                    do_modify_down(scen, csv);
                    break;
                }
            }
        }
        auto t_all1 = Clock::now();
        double secs = std::chrono::duration<double>(t_all1 - t_all0).count();
        std::cout << "\n[poisson] total_ops=" << ops << "  elapsed=" << secs
                << "s  throughput=" << (ops / secs) << " ops/s\n";
    }
};

int main(int argc, char **argv) {
    std::string scenario = (argc >= 2) ? argv[1] : "burst";
    std::size_t ops = (argc >= 3) ? static_cast<std::size_t>(std::stoull(argv[2])) : 100000;
    std::uint64_t seed = (argc >= 4) ? std::stoull(argv[3]) : 42;

    Bench B(seed);
    Csv csv("bench_results.csv");

    if (scenario == "burst") {
        B.run_burst(ops, csv);
    } else if (scenario == "poisson") {
        B.run_poisson(ops, csv);
    } else {
        std::cerr << "Unknown scenario: " << scenario << " (use: burst | poisson)\n";
        return 2;
    }

    std::cout << "\n=== per-op latency percentiles ===\n";
    for (auto &[name, stat]: B.S) stat.summary(name);
    std::cout << "CSV -> bench_results.csv\n";
    return 0;
}
