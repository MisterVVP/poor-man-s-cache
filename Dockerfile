FROM alpine:latest AS build
RUN apk update && apk upgrade && apk add git cmake build-base gtest-dev zlib-dev bash go

WORKDIR /app
COPY . .

# Number of elements to test
ENV NUM_ELEMENTS=10000000
# Run all tests
RUN bash /app/scripts/run-all-tests.bash

ARG BUILD_TYPE="Release"
RUN mkdir build && cd build && cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=$BUILD_TYPE && cd /app/build && cmake --build .
# Build the cluster controller from within its module directory so Go can resolve go.mod
RUN cd controller && go build -o /app/pmc-cluster-controller .


FROM alpine:latest

RUN apk update && apk upgrade && apk add libstdc++ 

COPY --from=build /app/build/src/poor-man-s-cache /app/poor-man-s-cache
COPY --from=build /app/pmc-cluster-controller /app/pmc-cluster-controller

EXPOSE 9001
EXPOSE 9100
EXPOSE 9400

RUN addgroup -g 10001 notroot \
    && adduser -u 10001 -G notroot -h /app -s /sbin/nologin -D poor-man-s-cache

USER 10001

ENTRYPOINT ["/app/poor-man-s-cache"]