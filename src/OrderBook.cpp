#include "OrderBook.h"

namespace exchange {

OrderBook::OrderBook() {
    // TODO: pre-warm order pool if needed
}

void OrderBook::submitOrder(const orderPacket& pkt, std::vector<Event>& out) {
    (void)pkt;
    (void)out;
    // TODO: validate packet, acquire Order from pool, insert into book, match
}

void OrderBook::cancelOrder(uint64_t order_id, std::vector<Event>& out) {
    (void)order_id;
    (void)out;
    // TODO: look up order, remove from level, release to pool, emit cancel ack/reject
}

void OrderBook::snapshot(std::vector<Event>& out) const {
    (void)out;
    // TODO: emit snapshot_start, snapshot_header, snapshotEntry rows, snapshot_end
}

}  // namespace exchange
