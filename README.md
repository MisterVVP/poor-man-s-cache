# poor-man-s-cache
When you got no money to buy enterprise and no desire to contribute to open source - build your own thing. Another pet project to practice.

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

## TODO
- Continue working on collision resolution
- Better memory management (unique_ptr? own mini garbage collector thread? both? check memory leaks and pointer usage?)
- Fix server code issues (check how others develop similar servers)
- Add key retention
- Check how table works with prime numbers vs normal numbers multiplied by 2
- Performance metrics (calculate under #ifndef NDEBUG)
- Nice to have things (configs from env variables, refactoring, splitting into different headers, e.t.c.)
