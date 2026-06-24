#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace exchange {

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class side : uint8_t {
    YES = 0,
    NO  = 1,
};

enum class action : uint8_t {
    SUBMIT = 0,
    CANCEL = 1,
};

enum class recieptType : uint8_t {
    order_ack        = 0,
    order_reject     = 1,
    cancel_ack       = 2,
    cancel_reject    = 3,
    trade_fill       = 4,
    self_trade       = 5,
    snapshot_header  = 6,
    snapshot_start   = 7,
    snapshot_end     = 8,
};

// ---------------------------------------------------------------------------
// Wire-format structs — packed, little-endian, largest fields first
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct orderPacket {
    uint64_t order_id;   // little-endian
    uint64_t user_id;    // little-endian
    uint64_t quantity;   // little-endian
    uint8_t  price;
    uint8_t  side;       // cast from enum side
    uint8_t  action;     // cast from enum action
};

struct tradeReciept {
    uint64_t maker_order_id;  // little-endian
    uint64_t taker_order_id;  // little-endian
    uint8_t  price;
    uint8_t  recieptType;     // cast from enum recieptType
    uint16_t _pad;
};

struct FillPacket {
    uint64_t order_id;         // little-endian
    uint64_t quantity_filled;  // little-endian
};

struct snapshotHeader {
    uint64_t entry_count;  // little-endian
    uint64_t _pad;
};

struct snapshotEntry {
    uint64_t quantity;  // little-endian
    uint8_t  price;
    uint8_t  side;      // cast from enum side
    uint16_t _pad;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Market constants
// ---------------------------------------------------------------------------

static constexpr uint8_t  MIN_PRICE = 1;
static constexpr uint8_t  MAX_PRICE = 99;


static_assert(sizeof(orderPacket)    == 27, "orderPacket size mismatch");
static_assert(sizeof(tradeReciept)   == 20, "tradeReciept size mismatch");
static_assert(sizeof(FillPacket)     == 16, "FillPacket size mismatch");
static_assert(sizeof(snapshotHeader) == 16, "snapshotHeader size mismatch");
static_assert(sizeof(snapshotEntry)  == 12, "snapshotEntry size mismatch");

} 
