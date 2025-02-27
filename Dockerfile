FROM alpine:latest AS build
RUN apk update && apk upgrade && apk add git cmake build-base gtest-dev zlib-dev
RUN git clone https://github.com/jupp0r/prometheus-cpp.git && cd prometheus-cpp \
    && git submodule init && git submodule update && mkdir _build && cd _build \
    && cmake .. -DBUILD_SHARED_LIBS=ON -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF \
    && cmake --build . --parallel 4 \
    && ctest -V \
    && cmake --install .

WORKDIR /app
COPY . .

# Number of elements to test
ENV NUM_ELEMENTS=100000
# Run hashtable tests
RUN cd /app/src && g++ -std=c++26 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ -L/usr/lib/ hash/*.cpp compressor/gzip_compressor.cpp kvs/*.cpp primegen/primegen.cpp -lz -lgtest -lgtest_main -o kvs_test && ./kvs_test
# Run compressor tests
RUN cd /app/src && g++ -std=c++26 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ compressor/*.cpp -lz -lgtest -lgtest_main -o test_gzip && ./test_gzip

ARG BUILD_TYPE="Release"
RUN mkdir build && cd build && cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=$BUILD_TYPE && cd /app/build && cmake --build .


FROM alpine:latest

RUN apk update && apk upgrade && apk add libstdc++ 

COPY --from=build /app/build/src/poor-man-s-cache /app/poor-man-s-cache
COPY --from=build /usr/local/include/prometheus/ /usr/local/include/prometheus/
COPY --from=build /usr/local/lib/ /usr/local/lib/

EXPOSE 9001
EXPOSE 8080

ENTRYPOINT ["/app/poor-man-s-cache"]