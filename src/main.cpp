#include <iostream>
#include "order_book.hpp"
using namespace me;

static int fails = 0;
#define REQUIRE(cond) do{ if(!(cond)){ std::cerr<<"[FAIL] line "<<__LINE__<<": " #cond "\n"; ++fails; } }while(0)
#define REQUIRE_EQ(a,b) do{ auto _a=(a); auto _b=(b); if(!(_a==_b)){ \
  std::cerr<<"[FAIL] line "<<__LINE__<<": " #a " == " #b " ("<<_a<<" vs "<<_b<<")\n"; ++fails; } }while(0)

void check_full_fill() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 100, 1});
    auto t = ob.add_limit({2, Side::Buy, OrdType::Limit, 101, 100, 2});
    REQUIRE_EQ(t.size(), 1u);
    REQUIRE_EQ(t[0].px, 100);
    REQUIRE_EQ(t[0].qty, 100);
    REQUIRE(!ob.best_ask().has_value());
}

void check_partial_then_post() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 50, 1});
    auto t = ob.add_limit({2, Side::Buy, OrdType::Limit, 101, 80, 2});
    REQUIRE_EQ(t.size(), 1u);
    REQUIRE_EQ(t[0].qty, 50);
    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE_EQ(bb->first, 101);
    REQUIRE_EQ(bb->second, 30);
}

void check_fifo() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 10, 1});
    ob.post_passive({2, Side::Sell, OrdType::Limit, 100, 20, 2});
    auto t = ob.add_limit({3, Side::Buy, OrdType::Limit, 101, 30, 3});
    REQUIRE_EQ(t.size(), 2u);
    REQUIRE_EQ(t[0].maker_id, 1);
    REQUIRE_EQ(t[1].maker_id, 2);
}

void check_modify_qty_down_in_place() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 50, 1});
    ob.post_passive({2, Side::Sell, OrdType::Limit, 100, 30, 2});
    auto t0 = ob.modify(2, std::nullopt, 10, 5);
    REQUIRE_EQ(t0.size(), 0u);
    auto t = ob.add_limit({3, Side::Buy, OrdType::Limit, 101, 60, 6});
    REQUIRE_EQ(t.size(), 2u);
    REQUIRE_EQ(t[0].maker_id, 1);
    REQUIRE_EQ(t[1].maker_id, 2);
    REQUIRE(!ob.best_ask().has_value());
}

void check_market_buy() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 50, 1});
    ob.post_passive({2, Side::Sell, OrdType::Limit, 101, 70, 2});
    auto t = ob.add_market({3, Side::Buy, OrdType::Market, 0, 60, 3});
    REQUIRE_EQ(t.size(), 2u);
    REQUIRE_EQ(t[0].px, 100);
    REQUIRE_EQ(t[0].qty, 50);
    REQUIRE_EQ(t[1].px, 101);
    REQUIRE_EQ(t[1].qty, 10);
    auto a = ob.best_ask();
    REQUIRE(a.has_value());
    REQUIRE_EQ(a->first, 101);
    REQUIRE_EQ(a->second, 60);
}

void check_cancel() {
    OrderBook ob;
    ob.post_passive({1, Side::Sell, OrdType::Limit, 100, 30, 1});
    ob.post_passive({2, Side::Sell, OrdType::Limit, 100, 40, 2});
    REQUIRE(ob.cancel(1));
    auto a = ob.best_ask();
    REQUIRE(a.has_value());
    REQUIRE_EQ(a->second, 40);
}

int main() {
    check_full_fill();
    check_partial_then_post();
    check_fifo();
    check_modify_qty_down_in_place();
    check_market_buy();
    check_cancel();

    if (fails == 0) {
        std::cout << "[OK] smoke passed\n";
        return 0;
    }
    std::cout << "[SUMMARY] fails=" << fails << "\n";
    return 1;
}
