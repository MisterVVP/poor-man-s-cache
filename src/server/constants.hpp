#pragma once
#include <cstdint>
#include <chrono>
#include <future>

#define EPOLL_WAIT_TIMEOUT_MSEC 100 // TODO: make configurable
#define MAX_EVENTS 2048 // TODO: make configurable
#define MAX_REQUEST_SIZE 536870912
#define READ_BUFFER_SIZE 16384

/// @brief Metrics update frequency, decrease for more up-to-date metrics, increase to save server resources
static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);

/// @brief Number of retries to read socket on EINTR
static constexpr uint_fast16_t READ_NUM_RETRY_ON_INT = 3;

/// @brief Number of retries to wait for epoll events on EINTR
static constexpr uint_fast16_t EPOLL_WAIT_NUM_RETRY_ON_INT = 3;

/// @brief Max attempts to read client data from socket, required to avoid endless loop
static constexpr uint_fast32_t READ_MAX_ATTEMPTS = (MAX_REQUEST_SIZE / READ_BUFFER_SIZE) * 2;

/// @brief Min response size to generate asynch response
static constexpr uint_fast32_t ASYNC_RESPONSE_SIZE_THRESHOLD = 1048576;

/// @brief Event batching delay - time to wait until next data arrival for batch processing
static constexpr long BATCHING_DELAY_NSEC = 500000; // 0.5 msec

/// @brief Starting number of event loop workers
static constexpr uint_fast32_t BATCH_MIN_SIZE = 32;

/// @brief Starting number of event loop workers
static constexpr unsigned int NUM_WORKERS = 4;

/// @brief Max client connection lifetime
static constexpr long MAX_CONN_LIFETIME_SEC = 300; // 5 min // TODO: make configurable

/// @brief Amount of time to wait while busy looping to accept connections
static constexpr std::chrono::nanoseconds ACCEPT_CONN_DELAY = std::chrono::nanoseconds(1);

/// @brief Amount of time to wait while busy looping to process requests
static constexpr std::chrono::nanoseconds PROCESS_REQ_DELAY = std::chrono::nanoseconds(1);