FROM alpine:3.19

RUN apk add --no-cache build-base

WORKDIR /app

COPY src/client/cache_client.hpp src/client/cache_client.hpp
COPY tests/client_integration/client_integration_test.cpp client_integration_test.cpp

RUN g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -pthread -Isrc client_integration_test.cpp -o client_integration_test

ENTRYPOINT ["/app/client_integration_test"]
