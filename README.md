# poor-man-s-cache

When you got no money to buy enterprise tooling and no desire to contribute to open source - build your own thing.
Another pet project to practice.

## Goals and philosophy

### Goals
- Build alternative to Redis cache from scratch. Requirements are limited to simple distributed keyvalue storage for string data.

### Philosophy (or means to achieve goals)
- Focus on performance, not readability. Use as little of standard std:: code as possible and prefer plain C-like code over ideomatic C++ approaches.
- Use modern C++ techniques when necessary and when I want to learn more about them (e.g. coroutines are interesting to learn, std::string is fast enough for a few operation, but should be avoided for highload)
- Prefer manual memory management instead of smart pointers


## Quick start

Run cache and tests
```
docker compose --profile main --profile tests build
docker compose --profile main up --detach
```
Wait a few moments for server to start. Check server container logs for `TCP server is ready to process incoming connections`

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
> [!WARNING]
> Debug mode is very slow! Performance could be 20 or 30 times slower. Valgrind (profiler) settings can be changed in docker-compose file under 'cache-valgrind' service configuration.
> [!TIP]
> Modify `BUILD_TYPE` build argument to switch between Debug (contains extra output to std and starts server much faster) and Release (optimized) builds
> [!TIP]
> Replace `valgrind` with helgrind to debug multithreading issues 

To run cache and tests:
```
docker compose --profile valgrind --profile tests-valgrind build
docker compose --profile valgrind up --detach
```
Wait a few moments for server to start (valgrind mode does not require long initialisation time). Check server container logs for `TCP server is ready to process incoming connections`

After server has started run test script
```
docker compose --profile tests-valgrind up
```
Check Valgrind output in container std during and after execution.


### To run callgrind profiler 

To run cache and tests:
```
docker compose --profile callgrind --profile tests-callgrind build
docker compose --profile callgrind up --detach
```
Wait a few minutes for server to start (callgrind slows down startup). Check server container logs for `TCP server is ready to process incoming connections`

After server has started run test script
```
docker compose --profile tests-callgrind up
```
> [!TIP]
> `ps aux` can help to find server process id inside cache-callgrind container

- exec into cache-callgrind container shell
- send termination signal to cache server process (`kill -s SIGTERM <server process id>`, e.g. `kill -s SIGTERM 7`)
- check Callgrind output in container stdout after execution.
- execute `callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes callgrind.out.<server process id>` (e.g. `callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes callgrind.out.7` )

> [!TIP]
> Results of callgrind_annotate command are hard to read without GUI. This repository does not provide any GUI example, but it's recommended to use (kcachegrind)[https://kcachegrind.github.io/html/Home.html]
> callgrind.out file can be found in /callgrind directory inside docker container



### To run only unit tests
> [!TIP]
> Use --build-arg GPP_FLAGS="-m64 -std=c++26 -O3 -DNDEBUG" to disable debugging helpers in code

```
docker build -f Dockerfile.utests --build-arg GPP_FLAGS="-m64 -std=c++26 -O3" . -t cache-tests:latest
docker run cache-tests:latest
```

### Various helpful shell commands
`sysctl -a` - check that all required sysctl options were overwritten successfully in docker
`netstat -an | grep 'TIME_WAIT' | wc -l` or `netstat -an | grep 'ESTABLISHED|CONNECTED' | wc -l` - check what's going on with sockets, useful during execution of python test script, example in sockmon.bash


### To check how redis works with the same task
```
docker compose --profile redis build
docker compose --profile redis up
```
Check redis metrics at http://localhost:9121/metrics

#### Results
> [!NOTE]
> There could be a way to configure Redis to work with the same amount of data, however I can not verify this without diving deep into Redis configuration, thus I am comparing against default redis configuration
> It is possible that there automatic is DDoS protection integrated at Redis as well, though I do not think this is fair to enable such functionality by default.
- Redis just stop processing requests normally at 10M or especially 100M operations. poor-man-s-cache works.
- Redis stopped responding normally even for 10000 requests sent from 4 threads. Typical redis error is Response: Socket error: [Errno 99] Address not available. This might be related to some antiDDOS protection of Redis
- Redis eats a small amount of memory, poor-man-s-cache eats plenty



## TODO
- Check if we can reduce memory usage during decompression as well
- Think about buffer size for incoming connections, at least make it configurable and add logic to read the data in chunks. Right now server won't work well if amount of data inside TCP request is bigger than our allocated buffer
- Continue improving collision resolution (endless task, tbh...)
- Review compression algorithm (also endless task, potential way to improve could be heuristical logic to determine optimal compression algo + storing a number of algos as a strategy pattern)
- Refactor how tests are executed and organised
- Support key expiration, support more operations
- Check out https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
- Refactoring of kvs.cpp, maybe extract hash function into separate header to reuse it as external function in multiple places... 
- Check if there are more neat ways of avoiding double hash calculation in server and kvs (right now we just provide extra public methods in kvs.cpp which accepts hash as an argument )

