# Design doc

## High level

```mermaid
graph TD
    A[TCP Client] --> B[ConnManager::acceptConnections]
    B --> C[epoll_wait loop]
    C -->|ready fd| D[readRequestAsync]
    D --> E[processRequest]
    E --> F[ServerShard::processCommand / processQuery]
    F --> G[KeyValueStore]
    E --> H[sendResponses]
    C --> I[metricsUpdaterThread] --> J[MetricsServer]
```

## Requests flow

### Current

```mermaid
sequenceDiagram
    participant Client
    participant Server
    participant Shard
    participant Store

    Client->>Server: Connect
    loop For each request
        Client->>Server: "SET key value␟"
        Server->>Server: readRequestAsync
        Server->>Shard: processRequest
        Shard->>Store: set(key, value)
        Store-->>Shard: result
        Shard-->>Server: "OK"
        Server->>Client: sendResponse
    end
```

### Pipelining

#### Concept

```mermaid
sequenceDiagram
    participant Client
    participant Server
    participant Shard
    participant Store

    Client->>Server: Connect
    Client->>Server: "SET k1 v1␟GET k1␟SET k2 v2␟"
    Server->>Server: readRequestAsync (gathers all commands)
    par Handle SET k1
        Server->>Shard: processRequest
        Shard->>Store: set(k1,v1)
        Store-->>Shard: OK
        Shard-->>Server: OK
    and Handle GET k1
        Server->>Shard: processRequest
        Shard->>Store: get(k1)
        Store-->>Shard: v1
        Shard-->>Server: v1
    and Handle DEL k1
        Server->>Shard: processRequest
        Shard->>Store: del(k1)
        Store-->>Shard: OK
        Shard-->>Server: OK
    end
    Server->>Client: sendResponses
```

#### Flow example

```mermaid
sequenceDiagram
    participant Client
    participant Server
    Client->>Server: "SET a 1"
    Client->>Server: "GET a"
    Client->>Server: "DEL a"
    Server->>Server: readRequestAsync (SET a 1)
    Server->>Server: enqueue for processing
    Server->>Server: readRequestAsync (GET a)
    Server->>Server: enqueue for processing
    Server->>Server: readRequestAsync (DEL a)
    Server->>Server: enqueue for processing
    par process SET
        Server->>Server: processRequest -> OK
    and process GET
        Server->>Server: processRequest -> "1"
    and process DEL
        Server->>Server: processRequest -> OK
    end
    Server->>Client: sendResponses("OK","1","OK")
```

### Performance improvements

Recent optimizations reduced the number of syscalls in pipelined mode. Requests
from a single connection are now processed in batches and all replies are sent
using `sendResponses`. With this approach the server reaches more than
**1 500 000** RPS for GET/DEL requests, over **1 000 000** RPS for SET and around
**3 000 000** RPS for the "SET key, GET key, GET non_existent_key" workflow on a
high‑end Linux machine.
