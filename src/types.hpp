#pragma once
#include <cstdint>

namespace me {
    enum class Side : uint8_t { Buy, Sell };

    enum class OrdType : uint8_t { Limit, Market };

    using Price = int64_t;
    using Qty = int64_t;
    using OrderId = uint64_t;
    using Ts = uint64_t;

    struct Order {
        OrderId id{};
        Side side{};
        OrdType type{OrdType::Limit};
        Price px{0};
        Qty qty{0};
        Ts ts{0};
    };

    struct Trade {
        OrderId taker_id{};
        OrderId maker_id{};
        Price px{};
        Qty qty{};
        Ts ts{};
    };
}
