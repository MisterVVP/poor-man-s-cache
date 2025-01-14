# poor-man-s-cache
When you got no money to buy enterprise and no desire to contribute to open source - build your own thing. Another pet project to practice.

## Quick start

Run cache and tests
```
docker compose build
docker compose up
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
- Check how table works with prime numbers vs normal numbers multiplied by 2
- Performance metrics (calculate under #ifndef NDEBUG)
- Nice to have things (configs from env variables, refactoring, splitting into different headers, e.t.c.)
