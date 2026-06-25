// using pragma pack to ensure there is no compiler padding btw struct members to avoid latency. Incase of network packets, the struct size need not be aligned by multiple of 8 bytes
// so no need of padding the struct members, and lower the bytes better the network/IPC latency.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

namespace exchange {

// the core engine is a normal limit order book over a single (YES) contract which only knows BUY (bid) and SELL (ask). 
// YES/NO prediction-market contract dimension is normalized away by the server gateway before orders reach the engine.
enum class Side : uint8_t {
    BUY  = 0,
    SELL = 1,
};

enum class Action : uint8_t {
    SUBMIT = 0,
    CANCEL = 1,
};

enum class recieptType : uint8_t {
    ORDER_ACK        = 0,
    ORDER_REJECT     = 1,
    CANCEL_ACK       = 2,
    CANCEL_REJECT    = 3,
    TRADE_FILL       = 4,
    SELF_TRADE       = 5,
    SNAPSHOT_HEADER  = 6,
    SNAPSHOT_DATA    = 7,
    SNAPSHOT_END     = 8,
};

#pragma pack(push, 1)

struct orderPacket {
    uint64_t order_id;
    uint64_t user_id;
    uint32_t quantity;
    uint16_t  price;
    Side  side;  
    Action  action;
};

static_assert(sizeof(orderPacket) == 24, "orderPacket must be exactly 24 bytes");

struct tradeReciept {
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    uint16_t  price;
    recieptType type;
};

static_assert(sizeof(tradeReciept) == 19, "tradeReciept must be exactly 19 bytes");

struct fillPacket {
    uint64_t order_id;
    uint32_t quantity_filled;
};

static_assert(sizeof(fillPacket) == 12, "fillPacket must be exactly 12 bytes");

struct snapshotHeader {
    uint32_t entry_count;
};

static_assert(sizeof(snapshotHeader) == 4, "snapshotHeader must be exactly 4 bytes");

struct snapshotData {
    uint32_t quantity;
    uint16_t  price;
    Side  side;
};

static_assert(sizeof(snapshotData) == 7, "snapshotData must be exactly 7 bytes");

#pragma pack(pop)

static constexpr std::size_t MAX_ORDERS = 65536;
static constexpr uint8_t  MIN_PRICE = 1;
static constexpr uint8_t  MAX_PRICE = 99;

} 
