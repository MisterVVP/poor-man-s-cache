#pragma once
#include <cstdint>
#include <chrono>
#include <future>

#define MSG_SEPARATOR 0x1F
#define EPOLL_WAIT_TIMEOUT -1
#define MAX_EVENTS 2048
#define MAX_REQUEST_SIZE 536870912
#define READ_BUFFER_SIZE 8192

/// @brief Server socket backlog, depends on tcp_max_syn_backlog, ignored when tcp_syncookies = 1
static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 2048;

/// @brief Metrics update frequency, decrease for more up-to-date metrics, increase to save server resources
static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);

/// @brief Number of retries on EINTR
static constexpr uint_fast16_t READ_NUM_RETRY_ON_INT = 3;

/// @brief Max attempts to read client data from socket, required to avoid endless loop
static constexpr uint_fast32_t READ_MAX_ATTEMPTS = (MAX_REQUEST_SIZE / READ_BUFFER_SIZE) * 2;

/// @brief Min response size to generate asynch response
static constexpr uint_fast32_t ASYNC_RESPONSE_SIZE_THRESHOLD = 1048576;
