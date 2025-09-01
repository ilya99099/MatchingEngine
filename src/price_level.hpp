#pragma once
#include <list>
#include <utility>
#include "types.hpp"

namespace me {
    struct PriceLevel {
        Price px{};
        std::list<Order> dq;
        Qty total{0};

        bool empty() const { return dq.empty(); }
        size_t size() const { return dq.size(); }

        const Order &front() const { return dq.front(); }
        Order &front() { return dq.front(); }

        std::list<Order>::iterator push(Order o) {
            total += o.qty;
            dq.push_back(std::move(o));
            auto it = dq.end();
            --it;
            return it;
        }

        void pop_front() {
            total -= dq.front().qty;
            dq.pop_front();
        }

        void erase(std::list<Order>::iterator it) {
            total -= it->qty;
            dq.erase(it);
        }
    };
}
