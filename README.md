# Binary Matching Engine

A limit-order matching engine for binary prediction markets (YES/NO contracts), exposed over a REST API.

## What I built

- A hybrid C++ matching engine with Node.js REST API server that maintains an order book with standard price–time priority: bids and asks, best price first, FIFO within each price level. Fills always execute at the resting (maker) price.
- A TypeScript REST gateway that sits in front of the engine, tracks order state for clients, and speaks to the engine over a Unix domain socket using a compact IPC protocol.
- YES/NO contract logic lives only on the gateway side. Buy/sell on YES map directly to engine bids/asks whereas the NO orders are complemented into their YES equivalent before hitting the engine (ex. buy NO @ 0.40 -> sell YES @ 0.60), and fill prices are translated back when responding to the client. That keeps the core engine as one simple instrument, bids and asks only, so it can be it's own seperate module anytime.
- Self-trade prevention: if an incoming order would match against the same user's resting order, we cancel the taker and leave the maker on the book. Partial fills against other users will still stand.
- Four REST endpoints for placing orders, cancelling, querying order status, and fetching an aggregated order-book snapshot, plus a health check so you can tell whether the gateway is connected to the engine.

## Why I built

- Incase of a self order/wash order sent, we cancel the taker. This was chosen because it’s far more easier than canceling and removing a resting order from the book or doing both, which could add runtime complexity if a user spams self orders.
- Having the contract abstraction i.e. YES/NO on just the server side makes the core engine much efficient and seperable as a completely different module. Having all the binary prediction market logic on the server side(where the NO contract’s buy and sell orders are complemented to its YES pair before sending it to the core engine and decoded back via order_id in ack receipts), helping the core engine to just have an order book with bids and asks of YES only. Thus, simplifying the core engine logic to any other common limit order matching dealing with bids/asks of a single instrument.

## How I built

- I started with the optimal system design approach based on the time period provided (2 days), where I found the overkill option would be a complete `C++` native engine and server framework due to it's latency. But this approach alone could take multiple weeks to design and test. Also, basic Node server with TS engine would solve the whole assignment in a day without much complexity, but the latency of the matching engine would be very high around 100-1000ms. So, I choose a hybrid approach of having the core engine in `C++` and the server in node as seperate independent modules supporting low latency speeds of 3ms as shown in the benchmark.
- For the two modules to communicate, I use a Unix socket with a IPC protocol to send order packets and ack packets between the engine and server in the same machine. This design helps if in the future we try to scale our engine to multi-threads and various shards.
- Keeping latency in mind, I also setup a memory pool to support MAX_RESTING_ORDERS(65536) (can be changed to higher value upon requirement) before the engine starts so that there isn't any time wasted to allocate space for incoming orders each sec. Thus, improving the latency and making it much more efficient.

## What I'd do if I had more time

- I would use native `C++` engine and `C++` web framework for the api endpoints improving the latency to be in microseconds depending on if I want the whole stack in `C++`.
- Add IP rate limiting on the server side to handle spam or ddos attacks.
- Leverage nodejs multi core arch using clustering and deploy multiple node workers with a load balancer increasing the throughput potential from 20k req/sec to 100k req/s as based on the engine benchmarks, the engine supports a throughput of 100k orders/s making this a vital improvement.
- As of now the core C++ engine runs on a single thread which supports upto 100k matches/sec, but with more time we can use multi-threads with sync(mutex locks or other race solutions) to support substantially more matching power by the core engine.
- Also, very much curious on implementing a multi-event market pair matching where one main market like “who wins?” has multiple options like “A” (yes/no), “B” (yes/no), "C" (yes/no) etc.

## Run it

```bash
docker compose up --build
```

Wait until the service is healthy, then hit **[http://localhost:3000](http://localhost:3000)**.

## Try the API

All prices are in dollars between **0.01** and **0.99** (e.g. `0.55` = 55¢). Quantity is a positive integer.

**1. Place a resting buy order**

```bash
curl -s -X POST http://localhost:3000/api/order \
  -H 'Content-Type: application/json' \
  -d '{"userId":"alice","side":"BUY","contract":"YES","price":0.55,"quantity":10}'
```

**2. Place a sell that crosses and fills against it**

```bash
curl -s -X POST http://localhost:3000/api/order \
  -H 'Content-Type: application/json' \
  -d '{"userId":"bob","side":"SELL","contract":"YES","price":0.55,"quantity":5}'
```

**3. Get the current order book**

```bash
curl -s http://localhost:3000/api/snapshot
```

**4. Look up an order (use the `orderId` from step 1 or 2)**

```bash
curl -s http://localhost:3000/api/order/1
```

## Benchmarks

**REST API** (tested with autocannon):

| Metric | Result |
|---|---|
| Avg. latency | 3 ms |
| Avg. throughput | 20k req/s |

**Core engine** (direct IPC, no REST gateway overhead):

| Metric | Result |
|---|---|
| Avg. latency | 0.032 ms (32 µs) |
| Avg. throughput | 95k orders/s |

