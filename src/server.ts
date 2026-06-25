import express from "express";
import type { Request, Response } from "express";
import net from "net";

const HTTP_PORT = Number(process.env.HTTP_PORT ?? 3000);
const ENGINE_SOCKET = process.env.ENGINE_SOCKET ?? "/tmp/exchange.sock";

type Contract  = "YES" | "NO";
type Side      = "BUY" | "SELL";
type OrderStatus = "open" | "partial" | "filled" | "cancelled";

interface OrderState {
  orderId:      string;
  userId:       string;
  contract:     Contract;
  side:         Side;
  price:        number;  
  quantity:     number;
  filledQty:    number;
  avgExecPrice?: number;
  status:       OrderStatus;
}

const orders = new Map<string, OrderState>();

// to have the engine just understand bids and asks, we normalize the YES / NO contracts orders to engine BUY / SELL (YES contract only)
//   Buy  YES @ p ->  engine BUY  @ p          (bid for YES)
//   Sell YES @ p ->  engine SELL @ p          (ask for YES)
//   Buy  NO  @ q ->  engine SELL @ (100 - q)  (buy NO = sell YES complement)
//   Sell NO  @ q ->  engine BUY  @ (100 - q)  (sell NO = buy YES complement)

function normalize(side: Side, contract: Contract, price: number) {
  return contract === "YES" ? { engineSide: side === "BUY" ? 0 : 1, enginePrice: price } 
  : { engineSide: side === "BUY" ? 1 : 0, enginePrice: 100 - price };
}

const centsToDollars = (cents: number): number => cents / 100;

// engine fills always execute at the maker's price
function execPriceForOrder(order: OrderState, makerPriceYes: number): number {
  return order.contract === "YES" ? makerPriceYes : 1 - makerPriceYes;
}

function recordFill(order: OrderState, execPrice: number, qty: number): void {
  const prevQty = order.filledQty;
  order.filledQty += qty;
  order.avgExecPrice =
    prevQty === 0 ? execPrice : ((order.avgExecPrice ?? 0) * prevQty + execPrice * qty) / order.filledQty;
  order.status = order.filledQty >= order.quantity ? "filled" : "partial";
}

// monotonic user/order-id counter
let nextId = 1n;
const allocId = (): bigint => nextId++;

function hashUserId(s: string): bigint {
  let h = 5381n;
  for (let i = 0; i < s.length; i++)
    h = ((h << 5n) + h + BigInt(s.charCodeAt(i))) & 0xFFFF_FFFF_FFFF_FFFFn;
  return h;
}

// encode a packet into a byte buffer
function encodePacket( orderId: bigint, userId: bigint, price: number, quantity: number, engineSide: number, action: number): Buffer {
  const b = Buffer.allocUnsafe(24);
  b.writeBigUInt64LE(orderId, 0);
  b.writeBigUInt64LE(userId, 8);
  b.writeUInt32LE(quantity, 16);
  b.writeUInt16LE(price, 20);
  b.writeUInt8(engineSide, 22);
  b.writeUInt8(action, 23);
  return b;
}

const RT = { ORDER_ACK: 0, ORDER_REJECT: 1, CANCEL_ACK: 2, CANCEL_REJECT: 3, TRADE_FILL: 4, SELF_TRADE: 5, SNAPSHOT_HEADER: 6,
             SNAPSHOT_DATA: 7, SNAPSHOT_END: 8 } as const;

interface Fill { makerOrderId: string; makerPrice: number; quantity: number; }
interface SelfTrade { makerOrderId: string; makerPrice: number; quantity: number; }
interface DepthLevel { price: number; quantity: number; }
interface ParsedBatch {
  ackType?:    number;
  ackOrderId?: string;
  ackQty?:     number;
  fills:       Fill[];
  selfTrade?:  SelfTrade;
  bids:        DepthLevel[];
  asks:        DepthLevel[];
}

function parseBatch(buf: Buffer): ParsedBatch {
  let off = 0;
  const count = buf.readUInt32LE(off); off += 4;
  const result: ParsedBatch = { fills: [], bids: [], asks: [] };

  for (let i = 0; i < count; i++) {
    const type = buf.readUInt8(off++);

    if (type <= RT.SELF_TRADE) {
      const makerOrderId = buf.readBigUInt64LE(off).toString(); off += 8;
      const takerOrderId = buf.readBigUInt64LE(off).toString(); off += 8;
      const priceCents   = buf.readUInt16LE(off); off += 2;
      off += 1; // redundant type byte inside tradeReciept
      const quantity     = buf.readUInt32LE(off); off += 4;
      const makerPrice   = centsToDollars(priceCents);

      if (type === RT.ORDER_ACK || type === RT.ORDER_REJECT || type === RT.CANCEL_ACK || type === RT.CANCEL_REJECT) {
        result.ackType    = type;
        result.ackOrderId = takerOrderId;
        result.ackQty     = quantity;
      } else if (type === RT.SELF_TRADE) {
        result.selfTrade = { makerOrderId, makerPrice, quantity };
      } else {
        result.fills.push({ makerOrderId, makerPrice, quantity });
      }
    } else if (type === RT.SNAPSHOT_HEADER) {
      off += 4; // entry_count not needed
    } else if (type === RT.SNAPSHOT_DATA) {
      const quantity = buf.readUInt32LE(off); off += 4;
      const price    = buf.readUInt16LE(off); off += 2;
      const side     = buf.readUInt8(off++);
      (side === 0 ? result.bids : result.asks).push({ price, quantity });
    }
    // SNAPSHOT_END has no body so do nothing
  }
  return result;
}

let sock: net.Socket | null = null;
let engineConnected = false;
let inbuf = Buffer.alloc(0);
const pending: Array<{ resolve: (b: Buffer) => void; reject: (e: Error) => void }> = [];

function onData(chunk: Buffer): void {
  inbuf = Buffer.concat([inbuf, chunk]);
  while (inbuf.length >= 4) {
    const frameLen = inbuf.readUInt32LE(0);
    if (inbuf.length < 4 + frameLen) break;
    const frame = inbuf.subarray(4, 4 + frameLen);
    inbuf = inbuf.subarray(4 + frameLen);
    pending.shift()?.resolve(Buffer.from(frame));
  }
}

function onDisconnect(err?: Error): void {
  if (!engineConnected && !sock) return;
  engineConnected = false;
  sock?.destroy();
  sock = null;
  inbuf = Buffer.alloc(0);
  const e = err ?? new Error("engine disconnected");
  pending.splice(0).forEach(p => p.reject(e));
  console.warn(`engine IPC disconnected (${e.message}), reconnecting in 1s`);
  setTimeout(connectToEngine, 1000);
}

function connectToEngine(): void {
  const s = net.createConnection({ path: ENGINE_SOCKET });
  s.once("connect", () => {
    sock = s;
    engineConnected = true;
    console.log("engine IPC connected");
  });
  s.on("data", onData);
  s.once("error", err => onDisconnect(err));
  s.once("close",  ()  => onDisconnect());
}

function sendFrame(payload: Buffer): Promise<Buffer> {
  if (!sock || !engineConnected)
    return Promise.reject(new Error("engine not connected"));
  const hdr = Buffer.allocUnsafe(4);
  hdr.writeUInt32LE(payload.length, 0);
  return new Promise((resolve, reject) => {
    pending.push({ resolve, reject });
    sock!.write(Buffer.concat([hdr, payload]));
  });
}

// REST API endpoints
const app = express();
app.use(express.json());

// POST /api/order  { userId, side, contract, price, quantity }
app.post("/api/order", async (req: Request, res: Response): Promise<void> => {
  
  const { userId, side, contract, price, quantity } = req.body as {
    userId?: string; side?: Side; contract?: Contract;
    price?: number; quantity?: number;
  };
  const priceCents = price != null ? Math.round(price * 100) : NaN; // convert price to integer cents for the engine

  if (!userId || !["BUY", "SELL"].includes(side as string) || !["YES", "NO"].includes(contract as string) ||
      typeof price !== "number" || priceCents < 1 || priceCents > 99 ||
      !Number.isInteger(quantity) || quantity! <= 0) {
    res.status(400).json({ error: "invalid body: required { userId, side: BUY|SELL, contract: YES|NO, price: 0.01-0.99, quantity: >0 }" });
    return;
  }

  const orderId = allocId();
  const { engineSide, enginePrice } = normalize(side!, contract!, priceCents);

  const state: OrderState = {
    orderId: orderId.toString(), userId: userId!, contract: contract!, side: side!, price: price!,
    quantity: quantity!, filledQty: 0, status: "open",
  };
  orders.set(state.orderId, state);

  try {
    const frame = await sendFrame(encodePacket(orderId, hashUserId(userId!), enginePrice, quantity!, engineSide, 0));
    const batch = parseBatch(frame);

    if (batch.ackType === RT.ORDER_REJECT) {
      orders.delete(state.orderId);
      res.status(422).json({ error: "order rejected by engine" });
      return;
    }

    const responseFills = batch.fills.map(f => {
      const maker = orders.get(f.makerOrderId);
      const makerPrice = maker ? execPriceForOrder(maker, f.makerPrice) : f.makerPrice;
      const takerPrice = execPriceForOrder(state, f.makerPrice);

      recordFill(state, takerPrice, f.quantity);
      if (maker) recordFill(maker, makerPrice, f.quantity);

      return {
        makerOrderId: f.makerOrderId,
        quantity: f.quantity,
        makerPrice,
        takerPrice,
      };
    });

    if (batch.selfTrade) {
      if (state.filledQty === 0) {
        state.status = "cancelled";
        res.status(422).json({
          error: "self-trade prevented",
          orderId: state.orderId,
          status: state.status,
          filledQty: 0,
          blockedBy: batch.selfTrade.makerOrderId,
        });
        return;
      }
      state.status = "partial";
      res.status(201).json({
        orderId: state.orderId,
        status: state.status,
        filledQty: state.filledQty,
        avgExecPrice: state.avgExecPrice,
        fills: responseFills,
        selfTradeBlocked: {
          blockedBy: batch.selfTrade.makerOrderId,
          blockedQty: batch.selfTrade.quantity,
        },
      });
      return;
    }

    if (state.filledQty === 0) state.status = "open";

    res.status(201).json({
      orderId: state.orderId,
      status: state.status,
      filledQty: state.filledQty,
      avgExecPrice: state.avgExecPrice,
      fills: responseFills,
    });
  } catch {
    orders.delete(state.orderId);
    res.status(503).json({ error: "engine unavailable" });
  }
});

// DELETE /api/order/:orderId?userId=...  (or in body: { userId })
app.delete("/api/order/:orderId", async (req: Request, res: Response): Promise<void> => {
  const userId = (req.body as { userId?: string })?.userId ?? (typeof req.query.userId === "string" ? req.query.userId : undefined);
  if (!userId) {
    res.status(400).json({ error: "userId required (query or body)" });
    return;
  }

  const state = orders.get(req.params.orderId);
  if (!state || state.status === "filled" || state.status === "cancelled") {
    res.status(404).json({ error: "order not found or already closed" });
    return;
  }
  // check if the order belongs to the user
  if (state.userId !== userId) {
    res.status(403).json({ error: "not your order" });
    return;
  }

  try {
    const frame = await sendFrame(encodePacket(BigInt(req.params.orderId), 0n, 0, 0, 0, 1));
    const batch = parseBatch(frame);
    if (batch.ackType === RT.CANCEL_REJECT) {
      res.status(422).json({ error: "cancel rejected by engine" });
      return;
    }

    state.status = "cancelled";
    res.json({ orderId: state.orderId, status: "cancelled", cancelledQty: batch.ackQty ?? 0 });
  } catch {
    res.status(503).json({ error: "engine unavailable" });
  }
});

// GET /api/snapshot
app.get("/api/snapshot", async (_req: Request, res: Response): Promise<void> => {
  try {
    const frame = await sendFrame(Buffer.alloc(0)); // empty payload is nothing but a request for a snapshot
    const { bids, asks } = parseBatch(frame);
    const toDollars = (levels: DepthLevel[]) =>
      levels.map(l => ({ price: centsToDollars(l.price), quantity: l.quantity }));
    res.json({ bids: toDollars(bids), asks: toDollars(asks) });
  } catch {
    res.status(503).json({ error: "engine unavailable" });
  }
});

// GET /api/order/:orderId
app.get("/api/order/:orderId", (req: Request, res: Response): void => {
  const state = orders.get(req.params.orderId);
  if (!state) { res.status(404).json({ error: "order not found" }); return; }
  res.json(state);
});

// GET /health
app.get("/health", (_req: Request, res: Response): void => {
  res.status(engineConnected ? 200 : 503)
     .json({ status: engineConnected ? "ok" : "degraded", engineConnected });
});

connectToEngine();
app.listen(HTTP_PORT, () => console.log(`gateway listening on :${HTTP_PORT}`));
