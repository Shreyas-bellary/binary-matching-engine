#pragma once

#include "MemoryPool.h"
#include "Types.h"

#include <cstdint>
#include <type_traits>
#include <cassert>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace exchange {

struct Order {
    uint64_t order_id;
    uint64_t user_id;
    uint64_t timestamp_ns;
    uint32_t quantity;      
    uint32_t original_qty;  
    uint16_t price;         
    Side     side;
};

static_assert(sizeof(Order) == 40, "Order must be exactly 40 bytes");
static_assert(std::is_trivially_destructible_v<Order>, "Order must be trivially destructible");

struct Event {
    recieptType type;
    uint32_t quantity; // carries the affected size (fill size, cancelled qty, level depth) or 0 when not applicable
    union Payload {
        tradeReciept    trade;
        fillPacket      fill;
        snapshotHeader  snapshot_header;
        snapshotData    snapshot_data;
    } payload;
};

class OrderBook {

private:
    std::unordered_map<uint64_t, Order*> orders_by_id;
    MemoryPool<Order, MAX_ORDERS> order_pool;

    using LevelQueue = std::deque<Order*>;
    using BidMap = std::map<uint16_t, LevelQueue, std::greater<uint16_t>>;
    using AskMap = std::map<uint16_t, LevelQueue, std::less<uint16_t>>;

    BidMap bid_levels;  // resting buys
    AskMap ask_levels;  // resting sells

    [[nodiscard]] bool validateSubmit(const orderPacket& pkt) const noexcept;

    // match the taker against the opposite side. returns true if the taker was cancelled by self-trade prevention
    template <typename Book>
    bool matchTaker(Order& taker, Book& opposite, std::vector<Event>& out);

    // insert the taker's unfilled quantity as a resting order. returns false if the pool is exhausted
    bool restRemainder(const Order& taker, std::vector<Event>& out);
    void removeFromBook(Order* order) noexcept;

public:
    OrderBook();

    // process an incoming order and append generated events to out vector
    void submitOrder(const orderPacket& pkt, std::vector<Event>& out);

    // cancel an existing order and append generated events to out vector
    void cancelOrder(uint64_t order_id, std::vector<Event>& out);

    // emit a full depth snapshot as a sequence of events
    void snapshot(std::vector<Event>& out) const;

    };
}
