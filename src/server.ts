import express, { Request, Response } from "express";
import net from "net";

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

const HTTP_PORT = Number(process.env.HTTP_PORT ?? 3000);
const ENGINE_SOCKET = process.env.ENGINE_SOCKET ?? "/tmp/exchange.sock";

// ---------------------------------------------------------------------------
// Sequential uint64 order-id generator (module-level BigInt counter)
// ---------------------------------------------------------------------------

let nextOrderId = 1n;

function allocateOrderId(): bigint {
  const id = nextOrderId;
  nextOrderId += 1n;
  return id;
}

// ---------------------------------------------------------------------------
// In-memory order state (updated on trade_fill / cancel events)
// ---------------------------------------------------------------------------

type Side = "YES" | "NO";

interface OrderState {
  orderId: string;
  userId: string;
  price: number;
  quantity: number;
  originalQty: number;
  side: Side;
  filledQty: number;
  status: "open" | "partial" | "filled" | "cancelled";
}

const orderState = new Map<string, OrderState>();

// ---------------------------------------------------------------------------
// Raw Unix-socket IPC client with length-prefixed framing (no external libs)
// ---------------------------------------------------------------------------

let engineConnected = false;
let ipcSocket: net.Socket | null = null;

function connectToEngine(): void {
  // TODO: create net.Socket, connect to ENGINE_SOCKET, attach data/error/close handlers
  // TODO: reconnection backoff on disconnect
}

function sendFrame(payload: Buffer): Promise<Buffer> {
  // TODO: write 4-byte little-endian length prefix + payload, await response frame
  return Promise.reject(new Error("TODO: sendFrame not implemented"));
}

function handleEngineEvent(frame: Buffer): void {
  // TODO: decode receipt events and update orderState on trade_fill / cancel ack
  void frame;
}

// ---------------------------------------------------------------------------
// REST gateway
// ---------------------------------------------------------------------------

const app = express();
app.use(express.json());

// POST /api/order  { userId, price, quantity, side: "YES"|"NO" }
app.post("/api/order", async (req: Request, res: Response) => {
  const { userId, price, quantity, side } = req.body as {
    userId?: string;
    price?: number;
    quantity?: number;
    side?: Side;
  };

  // TODO: validate body, allocateOrderId(), build orderPacket frame, sendFrame()
  // TODO: store initial OrderState in orderState map
  void userId;
  void price;
  void quantity;
  void side;

  res.status(501).json({ error: "TODO: not implemented" });
});

// DELETE /api/order/:orderId
app.delete("/api/order/:orderId", async (req: Request, res: Response) => {
  const { orderId } = req.params;

  // TODO: look up orderState, build cancel frame, sendFrame()
  void orderId;

  res.status(501).json({ error: "TODO: not implemented" });
});

// GET /api/snapshot  — aggregated YES / NO depth book
app.get("/api/snapshot", async (_req: Request, res: Response) => {
  // TODO: send snapshot request frame to engine, aggregate snapshotEntry events
  res.status(501).json({ error: "TODO: not implemented" });
});

// GET /api/order/:orderId  — order state from in-memory map
app.get("/api/order/:orderId", (req: Request, res: Response) => {
  const { orderId } = req.params;
  const state = orderState.get(orderId);

  if (!state) {
    res.status(404).json({ error: "order not found" });
    return;
  }

  res.json(state);
});

// GET /health  — Docker health check + engine connection status
app.get("/health", (_req: Request, res: Response) => {
  res.status(200).json({
    status: engineConnected ? "ok" : "degraded",
    engineConnected,
  });
});

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

connectToEngine();

app.listen(HTTP_PORT, () => {
  console.log(`Gateway listening on :${HTTP_PORT}`);
});
