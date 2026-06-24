#pragma once

#include "MemoryPool.h"
#include "Types.h"

#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace exchange {

struct Order {
    uint64_t order_id;
    uint64_t user_id;
    uint8_t  price;
    uint64_t quantity;
    uint64_t original_qty;
    side     side;
    uint64_t timestamp_ns;
};

// ---------------------------------------------------------------------------
// Engine output event
// ---------------------------------------------------------------------------

struct Event {
    recieptType type;

    union Payload {
        tradeReciept    trade;
        FillPacket      fill;
        snapshotHeader  snapshot_header;
        snapshotEntry   snapshot_entry;
    } payload;
};

// ---------------------------------------------------------------------------
// Binary-outcome order book
//
// Manages a binary-outcome market with two complementary sides:
//   buying YES at price p matches selling NO at price (100 - p) cents.
//
// Maintains separate price-level maps for YES_BUY and NO_BUY orders,
// with cross-book matching.
// ---------------------------------------------------------------------------

class OrderBook {
public:
    OrderBook();

    // Process an incoming order; append generated events to `out`.
    void submitOrder(const orderPacket& pkt, std::vector<Event>& out);

    // Cancel an existing order; append generated events to `out`.
    void cancelOrder(uint64_t order_id, std::vector<Event>& out);

    // Emit a full depth snapshot as a sequence of events.
    void snapshot(std::vector<Event>& out) const;

private:
    using LevelQueue = std::deque<Order*>;
    using PriceMap   = std::map<uint8_t, LevelQueue, std::greater<uint8_t>>;

    PriceMap yes_buy_levels_;  // YES bids, best (highest) price first
    PriceMap no_buy_levels_;   // NO bids,  best (highest) price first

    std::unordered_map<uint64_t, Order*> orders_by_id_;
    MemoryPool<Order, MAX_ORDERS> order_pool_;

    // TODO: cross-book matching helpers
    // TODO: self-trade prevention
    // TODO: price-level insertion / removal
};

}
