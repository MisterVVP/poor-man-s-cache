# poor-man-s-cache

When you got no money to buy enterprise tooling and no desire to contribute to open source - build your own thing.
Another pet project to practice.

![main](https://github.com/MisterVVP/poor-man-s-cache/actions/workflows/main.yml/badge.svg?branch=main)

## Goals and philosophy

### Goals
- Build an alternative to Redis cache from scratch. Requirements are limited to a simple distributed key-value storage for string data.

### Philosophy (or means to achieve goals)
- Focus on performance, not readability or some 'patterns' and idioms. Prefer plain C-like code when necessary. 
- Avoid excessive defensive programming and argument checking deep inside the server code. Validate the input from network, but do not validate anything after. For example: there is no point to validate pointers in kvs.get(ptr) method, because we should not have passed invalid pointer there! And if it was passed -> we should fix the 'outer' layer, not memory storage.
- Avoid using external libraries (e.g. boost), unless necessary. Exception: unit tests and non-core functionality (e.g. Prometheus metrics).
- Solution should support only Linux, preferrably alpine or similar distribution. No Windows or Mac OS support... ever.
- Solution should be container and (hopefully) orchestrator friendly

## Current progress

### Functional tests

#### Testing method
Local python script which is leveraging multiprocessing to send requests to the running server and await response from server.

There are few testing scenarios supported right now:
1. Multiple GET requests
2. Multiple SET requests
3. Multiple DEL requests
4. (SET key, GET key, GET non_existent_key) workflow
5. Pipelined sequence of commands over a single TCP connection

Functional RPS is calculated based on: (T<sub>client</sub> + T<sub>server</sub>) / N  
- T<sub>client</sub> - time spent to send all the requests by client + time to receive and verify the responses
- T<sub>server</sub> - time spent to process and respond to all the request by server
- N - total number of requests 

#### Test setups

Local setup (all with high end processor and half gbit internet) variations.
Lunix kernel settings used as much as possible for both local and docker setups can be found in local_server_setup.bash

1. Ubuntu
2. Docker on Ubuntu
3. Docker on Windows

CI setup (free github hosted runner hardware) variations
1. Default

#### Test details results
Local setup. 10 million requests per test suite. 24 logical threads.
1. > 100 000 RPS without pipelining, > 200 000 RPS with pipelining
2. ~ 90 000 RPS without pipelining, TBD with pipelining
3. TBD

CI setup. 1 million requests total (4 processes and 250000 chunks per process)
~ 22 500 RPS (without pipelining), > 100 000 RPS with pipelining

#### Goals
Next step is 500k+ functional RPS on Ubuntu (with our without pipelining)

#### How Redis works with the same task
Below are results that I got from using Redis.

1. Ubuntu

Installed via https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/apt/


##### Our own tests
```
python3 tcp_server_test.py --redis -p -b 16
```
**Results**:  ~ 110 000 RPS for GET / SET / DEL tests  
**Results with pipelining**:  ~ 500 000 RPS for GET / SET / DEL tests,  ~ 1 000 000 RPS for (SET key, GET key, GET non_existent_key) workflow tests

##### Redis benchmark
```
redis-benchmark -t set -r 1000000 -n 1000000 -d 12
```

**Results**:  ~ 110 000 RPS for GET / SET tests  
**Results with pipelining**:  ~ 1 000 000 RPS for GET / SET tests

2. Docker on Ubuntu
Run redis in docker
```
docker compose -f docker-compose-local.yaml --profile redis build
docker compose -f docker-compose-local.yaml --profile redis up
```

Run official redis-benchmark tool
```
docker exec 2d279699e307 redis-benchmark -t set -r 1000000 -n 1000000 -d 12
```

**Results**:  ~ 110 000 RPS for GET / SET tests

3. Docker on Windows

TODO: not verified

### Performance tests
TODO. Server performance (client agnostic) should be calculated on server. Can introduce PERF command into the protocol

### Unit tests

#### KeyValueStore
Local Ubuntu, 10M records:
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
docker compose -f docker-compose-local.yaml --profile main --profile tests build
docker compose -f docker-compose-local.yaml --profile main up --detach
```
Wait a few moments for the server to start. Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose -f docker-compose-local.yaml --profile tests up
```

You can check Prometheus metrics while tests are running by opening http://localhost:8080/metrics

Don't forget to shut the detached container down by issuing:
```
docker compose -f docker-compose-local.yaml --profile main down
```

#### To run only unit tests

```
docker build -f Dockerfile.utests . -t cache-tests:latest
docker run -it cache-tests:latest
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

Set env variables, for example:
```
source .env
```

Run unit tests:
```
./run-all-tests.bash
```
> [!TIP]
> Use Ctrl+C to send SIGTERM

Build app using Cmake extension for VsCode, then run command below (or click a button in VsCode CMAKE extension).
```
./out/build/Release/src/poor-man-s-cache
```

Setup python virtual environment and run python tests from tests folder. For example:
```
cd tests && \
virtualenv .venv && \
source .venv/bin/activate && \
pip install -r requirements.txt && \
python3 ./tcp_server_test.py
```
> [!TIP]
> You can change the number of request sequences in tests via export TEST_ITERATIONS=100000

#### Check memory leaks (valgrind)

Run (e.g. for Release build)
```
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose ./out/build/Release/src/poor-man-s-cache
```

Run python tests, e.g. from tests folder:
```
python3 ./tcp_server_test.py
```

#### Profiling (callgrind)
Profiling setup is similar for all Valgrind tools, below is an example for callgrind. For callgrind, Debug build is recommended, but not required.

Run (e.g. for Debug build)
```
valgrind --tool=callgrind --simulate-cache=yes ./out/build/Debug/src/poor-man-s-cache
```

Find pid, for example via `ps aux`. Callgrind will create a file called callgrind.out.<process id here>

Run python tests, e.g. from tests folder:
```
python3 ./tcp_server_test.py
```

After tests have finished, send SIGTERM to cache server and check callgrind output from callgrind.out.<process id here> file created by callgrind.
Open callgrind output file with [kcachegrind](https://kcachegrind.github.io/html/Home.html)

Else (if you are a samurai), you can try to figure things out from callgrind_annotate
```
callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes callgrind.out.<server process id>
```


### Kube Deployment
> [!TIP]
> You can replace namespace and release name.
> You can also supply different deployment configuration by editing values.yaml or providing arguments to helm, see [docs](https://helm.sh/docs/helm/helm_upgrade/).
> Use helm cmd arguments or edit image.tag in values.yaml to deploy different tag
```
helm upgrade --install poor-man-s-cache ./helm/poor-man-s-cache -n poor-man-s-cache --create-namespace
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
docker compose -f docker-compose-local.yaml --profile valgrind --profile tests-valgrind build
docker compose -f docker-compose-local.yaml --profile valgrind up --detach
```
Wait a few moments for the server to start (Valgrind mode does not require long initialization time). Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose -f docker-compose-local.yaml --profile tests-valgrind up
```
Check Valgrind output in container std during and after execution.

### To run Callgrind profiler

To run cache and tests:
```
docker compose -f docker-compose-local.yaml --profile callgrind --profile tests-callgrind build
docker compose -f docker-compose-local.yaml --profile callgrind up --detach
```
Wait a few minutes for the server to start (Callgrind slows down startup). Check server container logs for `TCP server is ready to process incoming connections`.

After the server has started, run the test script:
```
docker compose -f docker-compose-local.yaml --profile tests-callgrind up
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

## TODO
- Improve requests pipelining feature and server performance, focus on test cases with more than 10M requests
- Try some super fast hashtable (like the one from Google or boost), if it can increase performance by 20% -> use it, else just continue with the existing one and iterate on improvements.
- Test edge case scenarios
- Integrate valgrind checks into CI
- More corouties + refactor coroutine code to templates & other fancy things (if that won't hurt performance)
- Work on error responses from cache server
- Support key expiration, support more operations.
- Check if we can reduce memory usage during decompression as well.
- Integration between the main server and the metrics server can be improved.
- Continue improving collision resolution (endless task, tbh...).
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
