#!/bin/bash

valgrind --tool=callgrind --simulate-cache=yes /app/poor-man-s-cache &

echo "$!" > /tmp/cache.pid
cache_pid=$(cat /tmp/cache.pid)

sleep infinity &

wait $cache_pid

callgrind_annotate --tree=both --inclusive=yes --auto=yes --show-percs=yes /callgrind/callgrind.out.$cache_pid

exit $?
