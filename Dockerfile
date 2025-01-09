FROM alpine:latest AS build
RUN apk update && apk upgrade && apk add git cmake build-base
RUN git clone https://github.com/jupp0r/prometheus-cpp.git && cd prometheus-cpp \
    && git submodule init && git submodule update && mkdir _build && cd _build \
    && cmake .. -DBUILD_SHARED_LIBS=ON -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF \
    && cmake --build . --parallel 4 \
    && ctest -V \
    && cmake --install .

WORKDIR /app
COPY . .

RUN mkdir build && cd build && cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
RUN cd /app/build && cmake --build .


FROM alpine:latest
COPY ./sysctl.conf /etc/sysctl.conf

RUN apk update && apk upgrade && apk add libstdc++ && sysctl -p

COPY --from=build /app/build/src/poor-man-s-cache /app/poor-man-s-cache
COPY --from=build /usr/local/include/prometheus/ /usr/local/include/prometheus/
COPY --from=build /usr/local/lib/ /usr/local/lib/

EXPOSE 9001
EXPOSE 8080

ENTRYPOINT ["/app/poor-man-s-cache"]