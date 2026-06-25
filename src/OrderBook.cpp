#include "OrderBook.h"

#include <algorithm>
#include <cassert>
#include <chrono>

namespace exchange {

[[nodiscard]] static uint64_t nowNanos() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] static Event makeOrderEvent(recieptType type, uint64_t order_id, uint16_t price, uint32_t quantity) noexcept {
    Event ev{};
    ev.type     = type;
    ev.quantity = quantity;
    ev.payload.trade = tradeReciept{/*maker*/ 0, /*taker*/ order_id, price, type};
    return ev;
}

[[nodiscard]] static Event makeTradeEvent(recieptType type, uint64_t maker_id, uint64_t taker_id, uint16_t price, uint32_t quantity) noexcept {
    Event ev{};
    ev.type     = type;
    ev.quantity = quantity;
    ev.payload.trade = tradeReciept{maker_id, taker_id, price, type};
    return ev;
}

// remove a resting order from its price level and erase the level if it becomes empty
template <typename Book>
void eraseResting(Book& levels, Order* order) noexcept {
    auto level_it = levels.find(order->price);
    assert(level_it != levels.end() && "resting order's price level is missing");

    auto& queue = level_it->second;
    auto pos = std::find(queue.begin(), queue.end(), order);
    assert(pos != queue.end() && "resting order missing from its price level");
    queue.erase(pos);

    if (queue.empty()) {
        levels.erase(level_it);
    }
}

OrderBook::OrderBook() {
    orders_by_id.reserve(MAX_ORDERS);
}

bool OrderBook::validateSubmit(const orderPacket& pkt) const noexcept {
    if (pkt.action != Action::SUBMIT) {
        return false;
    }
    if (pkt.side != Side::BUY && pkt.side != Side::SELL) {
        return false;
    }
    if (pkt.quantity == 0) {
        return false;
    }
    if (pkt.price < MIN_PRICE || pkt.price > MAX_PRICE) {
        return false;
    }
    return true;
}

void OrderBook::submitOrder(const orderPacket& pkt, std::vector<Event>& out) {
    if (!validateSubmit(pkt)) {
        out.push_back(makeOrderEvent(recieptType::ORDER_REJECT, pkt.order_id, pkt.price, pkt.quantity));
        return;
    }

    // order ids must be unique across the live book
    if (orders_by_id.find(pkt.order_id) != orders_by_id.end()) {
        out.push_back(makeOrderEvent(recieptType::ORDER_REJECT, pkt.order_id, pkt.price, pkt.quantity));
        return;
    }

    out.push_back(makeOrderEvent(recieptType::ORDER_ACK, pkt.order_id, pkt.price, pkt.quantity)); // ACK the order

    Order taker{};
    taker.order_id     = pkt.order_id;
    taker.user_id      = pkt.user_id;
    taker.timestamp_ns = nowNanos();
    taker.quantity     = pkt.quantity;
    taker.original_qty = pkt.quantity;
    taker.price        = pkt.price;
    taker.side         = pkt.side;

    // try to match the taker against the opposite side
    const bool cancelled = (taker.side == Side::BUY) ? matchTaker(taker, ask_levels, out) : matchTaker(taker, bid_levels, out);
    if (cancelled) {
        return;  // self-trade prevention voided the remainder
    }

    if (taker.quantity > 0) {
        restRemainder(taker, out);
    }
}

template <typename Book>
bool OrderBook::matchTaker(Order& taker, Book& opposite, std::vector<Event>& out) {

    while (taker.quantity > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();  // best price on the opposite side
        const uint16_t maker_price = level_it->first;

        const bool crosses = (taker.side == Side::BUY) ? (taker.price >= maker_price) : (taker.price <= maker_price);
        if (!crosses) {
            break;
        }

        LevelQueue& queue = level_it->second;
        while (taker.quantity > 0 && !queue.empty()) {
            Order* maker = queue.front();

            // self-trade prevention: the incoming order is cancelled the moment it would trade against its own resting order
            // fills already done against other users still stand
            if (maker->user_id == taker.user_id) {
                out.push_back(makeTradeEvent(recieptType::SELF_TRADE, maker->order_id, taker.order_id, maker_price, taker.quantity));
                taker.quantity = 0;
                return true;
            }

            const uint32_t fill = std::min(taker.quantity, maker->quantity);
            out.push_back(makeTradeEvent(recieptType::TRADE_FILL, maker->order_id, taker.order_id, maker_price, fill));
            taker.quantity  -= fill;
            maker->quantity -= fill;

            if (maker->quantity == 0) {
                queue.pop_front();
                orders_by_id.erase(maker->order_id);
                order_pool.release(maker);
            }
        }

        if (queue.empty()) {
            opposite.erase(level_it);
        }
    }

    return false;
}

bool OrderBook::restRemainder(const Order& taker, std::vector<Event>& out) {
    Order* resting = order_pool.acquire();
    if (resting == nullptr) {
        // capacity exhausted
        out.push_back(makeOrderEvent(recieptType::ORDER_REJECT, taker.order_id, taker.price, taker.quantity));
        return false;
    }

    *resting = taker;
    if (resting->side == Side::BUY) {
        bid_levels[resting->price].push_back(resting);
    } else {
        ask_levels[resting->price].push_back(resting);
    }
    orders_by_id.emplace(resting->order_id, resting);
    return true;
}

void OrderBook::removeFromBook(Order* order) noexcept {
    if (order->side == Side::BUY) {
        eraseResting(bid_levels, order);
    } else {
        eraseResting(ask_levels, order);
    }
}

void OrderBook::cancelOrder(uint64_t order_id, std::vector<Event>& out) {
    auto it = orders_by_id.find(order_id);
    if (it == orders_by_id.end()) {
        out.push_back(makeOrderEvent(recieptType::CANCEL_REJECT, order_id, 0, 0));
        return;
    }

    Order* order             = it->second;
    const uint16_t price     = order->price;
    const uint32_t remaining = order->quantity;

    removeFromBook(order);
    orders_by_id.erase(it);
    order_pool.release(order);
    out.push_back(makeOrderEvent(recieptType::CANCEL_ACK, order_id, price, remaining));
}

void OrderBook::snapshot(std::vector<Event>& out) const {
    
    const auto entry_count = static_cast<uint32_t>(bid_levels.size() + ask_levels.size());

    Event header{};
    header.type = recieptType::SNAPSHOT_HEADER;
    header.payload.snapshot_header = snapshotHeader{entry_count};
    out.push_back(header);

    const auto emit_levels = [&out](const auto& levels, Side side) {
        for (const auto& [price, queue] : levels) {
            uint32_t level_qty = 0;
            for (const Order* o : queue) {
                level_qty += o->quantity;
            }
            Event row{};
            row.type     = recieptType::SNAPSHOT_DATA;
            row.quantity = level_qty;
            row.payload.snapshot_data = snapshotData{level_qty, price, side};
            out.push_back(row);
        }
    };

    emit_levels(bid_levels, Side::BUY);
    emit_levels(ask_levels, Side::SELL);

    Event end{};
    end.type = recieptType::SNAPSHOT_END;
    out.push_back(end);
}

}
