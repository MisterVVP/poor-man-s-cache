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
- Invent smart condition for performing full scan
- Add different rehashing mechanism for tables of more than 100 000 or N items, should be some heuristic to use different hashing strategies depending on table size
- Nice to have things (configs from env variables, refactoring, splitting into different headers, e.t.c.)