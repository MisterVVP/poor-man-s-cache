# poor-man-s-cache
When you got no money to buy enterprise tooling and no desire to contribute to open source - build your own thing.
Another pet project to practice.

## Goals and philosophy

### Goals
- Build alternative to Redis cache from scratch. Requirements are limited to simple distributed keyvalue storage for string data.

### Philosophy (or means to achieve goals)
- Choose relatively fast programming language, which I am relatively familiar with (Chose C++)
- Focus on performance, not readability. Use as little of standard std:: code as possible and prefer plain C-like code over ideomatic C++ approaches.
- Use modern C++ techniques when necessary and when I want to learn more about them (e.g. coroutines are interesting to learn, std::string is fast enough for a few operation, but should be avoided for highload)
- Prefer manual memory management instead of smart pointers


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
### To debug memory issues

Run cache (in debug mode) and tests
```
docker compose --profile debug --profile tests-debug build
docker compose --profile debug up --detach
```
Wait a few moments for server to start (debug mode does not require long initialisation time). Check server container logs for `TCP server is ready to process incoming connections`

After server has started run test script
```
docker compose --profile tests-debug up
```

Check valgrind output in container std during and after execution.


### To run only unit tests
!Use --build-arg GPP_FLAGS="-m64 -std=c++26 -O3 -DNDEBUG" to disable debug code!

```
docker build -f Dockerfile.utests --build-arg GPP_FLAGS="-m64 -std=c++26 -O3" . -t cache-tests:latest
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
- Add profiler
- Review compression algorithm
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
