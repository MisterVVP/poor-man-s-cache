# poor-man-s-cache

When you got no money to buy enterprise tooling and no desire to contribute to open source - build your own thing.
Another pet project to practice.

![main](https://github.com/MisterVVP/poor-man-s-cache/actions/workflows/main.yml/badge.svg?branch=main)

## Goals and philosophy

### Goals
- Build an alternative to Redis cache from scratch. Requirements are limited to a simple distributed key-value storage for string data.

### Philosophy (or means to achieve goals)
- Focus on performance, not readability or some 'patterns' and idioms. Prefer plain C-like code when necessary.
- Use modern C++ techniques when necessary and when I want to learn more about them (e.g. coroutines are interesting to learn, std::string should be avoided for high load, std::unordered_map is a complete no-no).
- Avoid using external libraries (e.g. boost), unless necessary. Exception: unit tests and non-core functionality (e.g. Prometheus metrics).
- Solution should support only Linux, preferrably alpine or similar distribution. No Windows or Mac OS support... ever.
- Solution should be container and (hopefully) orchestrator friendly

## Current progress

### Functional tests
- Server is able to handle 47000 requests per second on bare ubuntu (high end processor and half gbit internet). Next step is 100k+ requests per second
- 24000 RPS inside docker on Windows (same hardware as ubuntu above)
- 7000 RPS on poor github runner (likely within a container as well)

### Unit tests

#### KeyValueStore
- KeyValueStoreTest.LargeJSONFiles (1167 ms)
- KeyValueStoreTest.AddAndRetrieveElements (22795 ms)
- KeyValueStoreTest.OverwriteElements (45561 ms)

## Quick start
> [!TIP]
> Use `NUM_SHARDS` environment variable (for local setup it's defined in common-compose-config.yaml) to control the number of shards for the server.
> A high number of shards involves a small memory overhead, however, it may boost server performance and help to avoid collisions for large storage.

### Docker
Run cache and tests
```
docker compose --profile main --profile tests build
docker compose --profile main up --detach
```
Wait a few moments for the server to start. Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose --profile tests up
```

You can check Prometheus metrics while tests are running by opening http://localhost:8080/metrics

Don't forget to shut the detached container down by issuing:
```
docker compose --profile main down
```

### Local Ubuntu with sudo access
Open terminal in repository root and apply system configuration via
```
sudo bash ./local_server_setup.bash
```

Open second terminal somewhere on your hard drive and install required dependencies
```
sudo apt update && sudo apt upgrade -y
sudo apt install -y git cmake build-essential libgtest-dev zlib1g-dev gcc-14 g++-14

git clone https://github.com/jupp0r/prometheus-cpp.git && cd prometheus-cpp && \
git submodule init && git submodule update && \
mkdir _build && cd _build && \
cmake .. -DBUILD_SHARED_LIBS=ON -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF && \
cmake --build . --parallel $(nproc) && \
ctest -V && \
sudo cmake --install .
```

Run unit tests:
```
cd ./src && \
export NUM_ELEMENTS=10000 && \
g++-14 -std=c++23 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ -L/usr/lib/ hash/*.cpp compressor/gzip_compressor.cpp kvs/*.cpp primegen/primegen.cpp -lz -lgtest -lgtest_main -o kvs_test && ./kvs_test && \
g++-14 -std=c++23 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ compressor/*.cpp -lz -lgtest -lgtest_main -o test_gzip && ./test_gzip && \
cd ..
```


### To debug memory issues
> [!WARNING]
> Debug mode is very slow! Performance could be 20 or 30 times slower. Valgrind (profiler) settings can be changed in the docker-compose file under the 'cache-valgrind' service configuration.

> [!TIP]
> Modify the `BUILD_TYPE` build argument to switch between Debug (contains extra output to std and starts server much faster) and Release (optimized) builds.

> [!TIP]
> Replace `valgrind` with `helgrind` to debug multithreading issues.

To run cache and tests:
```
docker compose --profile valgrind --profile tests-valgrind build
docker compose --profile valgrind up --detach
```
Wait a few moments for the server to start (Valgrind mode does not require long initialization time). Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose --profile tests-valgrind up
```
Check Valgrind output in container std during and after execution.

### To run Callgrind profiler

To run cache and tests:
```
docker compose --profile callgrind --profile tests-callgrind build
docker compose --profile callgrind up --detach
```
Wait a few minutes for the server to start (Callgrind slows down startup). Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose --profile tests-callgrind up
```
> [!TIP]
> `ps aux` can help to find the server process ID inside the cache-callgrind container.

- Exec into the cache-callgrind container shell.
- Send a termination signal to the cache server process (`kill -s SIGTERM <server process id>`, e.g. `kill -s SIGTERM 7`).
- Check Callgrind output in container stdout after execution.
- Execute `callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes callgrind.out.<server process id>` (e.g. `callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes callgrind.out.7`).

> [!TIP]
> Results of the `callgrind_annotate` command are hard to read without a GUI. This repository does not provide any GUI example, but it's recommended to use [kcachegrind](https://kcachegrind.github.io/html/Home.html).
> The `callgrind.out` file can be found in the `/callgrind` directory inside the Docker container.

### To run only unit tests
> [!TIP]
> Use `--build-arg GPP_FLAGS="-m64 -std=c++23 -O3 -DNDEBUG"` to disable debugging helpers in code.
```
docker build -f Dockerfile.utests --build-arg GPP_FLAGS="-m64 -std=c++23 -O3" . -t cache-tests:latest
docker run -it cache-tests:latest
```

### Various helpful shell commands
`sysctl -a` - Check that all required sysctl options were overwritten successfully in Docker.
`netstat -an | grep 'TIME_WAIT' | wc -l` or `netstat -an | grep 'ESTABLISHED|CONNECTED' | wc -l` - Check what's going on with sockets, useful during execution of the Python test script (example in `sockmon.bash`).
`echo -ne "SET key1 value1\x1F" | nc localhost 9001` - Send a single SET request to cache server (nice for quick testing)
`echo -ne "GET key1\x1F" | nc localhost 9001` - Send a single GET request to cache server (nice for quick testing)

Netstat in a loop:
```
while :
do
    netstat -pan
    sleep 1
done
```

### To check how Redis works with the same task
```
docker compose --profile redis build
docker compose --profile redis up
```
Check Redis metrics at http://localhost:9121/metrics

#### Results
> [!NOTE]
> There could be a way to configure Redis to work with the same amount of data, however, I cannot verify this without diving deep into Redis configuration. Thus, I am comparing against the default Redis configuration.
> It is possible that there is automatic DDoS protection integrated into Redis as well, though I do not think it is fair to enable such functionality by default.

- Redis just stops processing requests normally in multithreaded high troughput scenario, poor-man's-cache works.
- Redis seems to have very low throughput
- Redis uses a small amount of memory, while poor-man's-cache consumes significantly more.

> [!NOTE]
> Redis Pipelines could be something to try out when testing against Redis, but it won't be fair comparison until we implement batch processing within a single connection

## TODO
> [!NOTE]
> Data compression is quite inneficient so far and is disabled by default

- Integrate valgrind checks into CI
- Add docker image to releases and create helm chart
- More corouties
- Work on error responses from cache server
- Check if we can reduce memory usage during decompression as well.
- Python code inside 'tests' folder deserves refactoring (low priority)
- Integration between the main server and the metrics server can be improved.
- Continue improving collision resolution (endless task, tbh...).
- Support key expiration, support more operations.
- Check if there are better ways of avoiding double hash calculation in the server and KVS (right now we just provide extra public methods in `kvs.cpp` which accept hash as an argument).
- There is an opportunity to try out Robot Framework for testing & writing test cases (I've never used that tool). OR just use [Cucumber for Golang aka Godog](https://github.com/cucumber/godog) tests, which I know.
- Write more documentation and describe the communication protocol.
- Try out a scaled multi-instance setup (this may require writing a custom load balancer or reverse proxy or using existing solutions like Nginx/Envoy/etc.).
- Check why Valgrind always shows a tiny memory leak from the Prometheus-cpp lib (`116 bytes in 1 block are still reachable in loss record 1 of 1`).
- Read http://www.kegel.com/c10k.html
- Continue reading https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/

## Questions / Ideas
- Store value size and increase memory usage? size_t will require extra 80 Mb per 10M records, however it'll eliminate many strelen() calls.


## Good articles and guidelines
- https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
- https://idea.popcount.org/2017-02-20-epoll-is-fundamentally-broken-12/
- https://copyconstruct.medium.com/the-method-to-epolls-madness-d9d2d6378642
- https://eklitzke.org/blocking-io-nonblocking-io-and-epoll
- [Simon Tatham, 2023 - Writing custom C++20 coroutine systems] (https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/).
