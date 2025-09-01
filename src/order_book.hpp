#pragma once
#include <map>
#include <optional>
#include <unordered_map>
#include "price_level.hpp"
#include <vector>
#include <cassert>

namespace me {
    class OrderBook {
    public:
        using Bids = std::map<Price, PriceLevel, std::greater<Price> >;
        using Asks = std::map<Price, PriceLevel, std::less<Price> >;
        using LevelIter = std::list<Order>::iterator;

        struct Handle {
            Side side{};
            Price px{};
            std::list<Order>::iterator it{};
        };

        Handle post_passive(Order o) {
            Side s = o.side;
            Price p = o.px;
            OrderId oid = o.id;

            auto &lvl = ensure_level(s, p);
            auto it = lvl.push(std::move(o));

            Handle h{s, p, it};
            by_id_[oid] = h;

#ifndef NDEBUG
            assert_invariants();
#endif
            return h;
        }

        std::vector<Trade> add_limit(Order o) {
            std::vector<Trade> out;
            if (o.side == Side::Buy) {
                match_buy(o, false, out);
            } else {
                match_sell(o, false, out);
            }
            if (o.qty > 0) post_passive(std::move(o));
#ifndef NDEBUG
            assert_invariants();
#endif
            return out;
        }

        std::vector<Trade> add_market(Order o) {
            o.type = OrdType::Market;
            std::vector<Trade> out;
            if (o.side == Side::Buy) {
                match_buy(o, true, out);
            } else {
                match_sell(o, true, out);
            }
#ifndef NDEBUG
            assert_invariants();
#endif
            return out;
        }

        std::vector<Trade> modify(OrderId id, std::optional<Price> new_px, std::optional<Qty> new_qty, Ts ts_now) {
            std::vector<Trade> trades;

            auto it_idx = by_id_.find(id);
            if (it_idx == by_id_.end()) return trades;

            Handle h = it_idx->second;
            PriceLevel *lvl = find_level(h.side, h.px);
            if (!lvl) {
                by_id_.erase(it_idx);
                return trades;
            }

            Order &ref = *(h.it);
            Price target_px = new_px.value_or(ref.px);
            Price target_qty = new_qty.value_or(ref.qty);

            if (target_qty <= 0) {
                lvl->erase(h.it);
                erase_level_if_empty(h.side, h.px);
                by_id_.erase(it_idx);
#ifndef NDEBUG
                assert_invariants();
#endif
                return trades;
            }

            bool price_changed = (target_px != ref.px);
            bool qty_up = (target_qty > ref.qty);

            if (!price_changed && !qty_up && target_qty < ref.qty) {
                Qty delta = ref.qty - target_qty;
                ref.qty = target_qty;
                lvl->total -= delta;
#ifndef NDEBUG
                assert_invariants();
#endif
                return trades;
            }

            Side side = ref.side;
            OrdType type = OrdType::Limit;

            lvl->erase(h.it);
            erase_level_if_empty(h.side, h.px);
            by_id_.erase(it_idx);

            Order fresh(id, side, type, target_px, target_qty, ts_now);
            auto deals = add_limit(fresh);
            trades.insert(trades.end(), deals.begin(), deals.end());
#ifndef NDEBUG
            assert_invariants();
#endif
            return trades;
        }

        std::optional<std::pair<Price, Qty> > best_bid() const {
            if (bids_.empty()) return std::nullopt;
            auto it = bids_.begin();
            return std::make_pair(it->first, it->second.total);
        }

        std::optional<std::pair<Price, Qty> > best_ask() const {
            if (asks_.empty()) return std::nullopt;
            auto it = asks_.begin();
            return std::make_pair(it->first, it->second.total);
        }

        bool cancel(OrderId id) {
            auto it = by_id_.find(id);
            if (it == by_id_.end()) return false;
            Handle h = it->second;

            auto &book = (h.side == Side::Buy) ? reinterpret_cast<MapRef &>(bids_) : reinterpret_cast<MapRef &>(asks_);
            auto *lvl = find_level(h.side, h.px);
            if (!lvl) {
                by_id_.erase(it);
                return false;
            }

            lvl->erase(h.it);
            erase_level_if_empty(h.side, h.px);
            by_id_.erase(it);
#ifndef NDEBUG
            assert_invariants();
#endif
            return true;
        }

    private:
        using MapRef = std::map<Price, PriceLevel>;

        Bids bids_;
        Asks asks_;
        std::unordered_map<OrderId, Handle> by_id_;

        PriceLevel &ensure_level(Side s, Price px) {
            if (s == Side::Buy) {
                auto [it, _] = bids_.try_emplace(px);
                it->second.px = px;
                return it->second;
            } else {
                auto [it, _] = asks_.try_emplace(px);
                it->second.px = px;
                return it->second;
            }
        }

        PriceLevel *find_level(Side s, Price px) {
            if (s == Side::Buy) {
                auto it = bids_.find(px);
                return it == bids_.end() ? nullptr : &it->second;
            } else {
                auto it = asks_.find(px);
                return it == asks_.end() ? nullptr : &it->second;
            }
        }

        void erase_level_if_empty(Side s, Price px) {
            if (s == Side::Buy) {
                auto it = bids_.find(px);
                if (it != bids_.end() && it->second.empty()) bids_.erase(it);
            } else {
                auto it = asks_.find(px);
                if (it != asks_.end() && it->second.empty()) asks_.erase(it);
            }
        }

        void match_buy(Order &taker, bool is_market, std::vector<Trade> &out) {
            while (taker.qty > 0 && !asks_.empty()) {
                auto it_level = asks_.begin();
                Price level_px = it_level->first;

                if (!is_market && level_px > taker.px) break;

                PriceLevel &lvl = it_level->second;

                while (taker.qty > 0 && !lvl.dq.empty()) {
                    Order &maker = lvl.dq.front();
                    OrderId maker_id = maker.id;
                    Qty fill = (taker.qty < maker.qty) ? taker.qty : maker.qty;

                    out.push_back(Trade{
                        taker.id,
                        maker_id,
                        level_px,
                        fill,
                        taker.ts
                    });

                    taker.qty -= fill;
                    maker.qty -= fill;
                    lvl.total -= fill;

                    if (maker.qty == 0) {
                        lvl.dq.pop_front();
                        by_id_.erase(maker_id);
                    }
                }

                if (lvl.empty()) {
                    asks_.erase(it_level);
                } else {
                    if (!is_market && it_level->first > taker.px) break;
                }
            }
        }

        void match_sell(Order &taker, bool is_market, std::vector<Trade> &out) {
            while (taker.qty > 0 && !bids_.empty()) {
                auto it_level = bids_.begin();
                Price level_px = it_level->first;

                if (!is_market && level_px < taker.px) break;

                PriceLevel &lvl = it_level->second;

                while (taker.qty > 0 && !lvl.dq.empty()) {
                    Order &maker = lvl.dq.front();
                    OrderId maker_id = maker.id;
                    Qty fill = (taker.qty < maker.qty) ? taker.qty : maker.qty;

                    out.push_back(Trade{
                        taker.id,
                        maker_id,
                        level_px,
                        fill,
                        taker.ts
                    });

                    taker.qty -= fill;
                    maker.qty -= fill;
                    lvl.total -= fill;

                    if (maker.qty == 0) {
                        lvl.dq.pop_front();
                        by_id_.erase(maker_id);
                    }
                }

                if (lvl.empty()) {
                    bids_.erase(it_level);
                } else {
                    if (!is_market && it_level->first < taker.px) break;
                }
            }
        }

#ifndef NDEBUG
            static Qty sum_level(const PriceLevel& lvl) {
                Qty s = 0;
                for (const auto& o : lvl.dq) s+=o.qty;
                return s;
            }

            void assert_side_invariants(const Bids& side) const {
                for (const auto& [px,lvl] : side) {
                    assert(lvl.px == px);
                    assert(!lvl.dq.empty());
                    assert(sum_level(lvl) == lvl.total);
                }
            }

            void assert_side_invariants(const Asks& side) const {
                for (const auto& [px,lvl] : side) {
                    assert(lvl.px == px);
                    assert(!lvl.dq.empty());
                    assert(sum_level(lvl) == lvl.total);
                }
            }

            void assert_invariants() const {
                assert_side_invariants(bids_);
                assert_side_invariants(asks_);
            }
#endif
    };
}
