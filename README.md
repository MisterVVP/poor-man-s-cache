# poor-man-s-cache
When you got no money to buy enterprise tooling and no desire to contribute to open source - build your own thing.
Another pet project to practice.

## Quick start

Run cache and tests
```
docker compose --profile main --profile tests build
docker compose --profile main up --detach
```
Wait a minute or two for server to start. Check server container logs for `TCP server is ready to process incoming connections`

After server has started run test script
```
docker compose --profile tests up
```

You can check prometheus metrics while tests are running by opening http://localhost:8080/metrics

Don't forget to shut detached container down by issuing

```
docker compose --profile main down
```

### To run only unit tests
```
docker build -f Dockerfile.utests . -t cache-tests:latest
docker run cache-tests:latest
```

### To check how redis works with the same task
```
docker compose --profile redis build
docker compose --profile redis up
```
Check redis metrics at http://localhost:9121/metrics

#### Results
- Redis just stop processing requests normally at 10M or especially 100M operations. poor-man-s-cache works.
- Redis eats a small amount of memory, poor-man-s-cache eats plenty
- Redis stopped responding normally even for 10000 requests sent from 4 threads. Typical redis error is Response: Socket error: [Errno 99] Address not available. This might be related to some antiDDOS protection of Redis



## TODO
- Check if we can reduce memory usage during decompression as well
- Continue improving collision resolution
- Move everything metrics related to metrics.h or maybe metrics.cpp
- Move everything server related to server.h or maybe server.cpp
- Better memory management (unique_ptr? own mini garbage collector thread? both? check memory leaks and pointer usage?)
- Improve server code
- Refactor how tests are executed and organised
- Add compression of stored key value pairs
- Add key retention
- Check how table works with prime numbers vs normal numbers multiplied by 2
- Performance metrics (calculate under #ifndef NDEBUG)
- Nice to have things (configs from env variables, refactoring, splitting into different headers, e.t.c.)
