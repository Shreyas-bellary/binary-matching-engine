FROM debian:bookworm-slim AS cpp-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/main.cpp src/OrderBook.cpp src/MemoryPool.cpp src/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel "$(nproc)"


FROM node:20-bookworm-slim AS ts-builder

WORKDIR /build
COPY package.json tsconfig.json ./
COPY src/server.ts src/

RUN npm install \
    && npm run build

FROM node:20-bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=cpp-builder /build/build/engine          /app/engine
COPY --from=ts-builder  /build/dist                 /app/gateway/dist
COPY --from=ts-builder  /build/node_modules         /app/gateway/node_modules
COPY --from=ts-builder  /build/package.json         /app/gateway/package.json
COPY scripts/start.sh                               /app/start.sh

RUN chmod +x /app/start.sh /app/engine

ENV HTTP_PORT=3000
ENV ENGINE_SOCKET=/tmp/exchange.sock

EXPOSE 3000
CMD ["/app/start.sh"]
