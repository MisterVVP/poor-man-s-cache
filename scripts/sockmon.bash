#!/bin/bash
echo 'Monitoring open sockets...'

trap quit SIGINT
quit() {
    exit 0
}

while :
do
    printf "Number of sockets in TIME_WAIT state: $(netstat -an | grep 'TIME_WAIT' | wc -l) \n"
    printf "Number of sockets in ESTABLISHED state: $(netstat -an | grep 'ESTABLISHED' | wc -l) \n"
    printf "Number of sockets in CONNECTED state: $(netstat -an | grep 'CONNECTED' | wc -l) \n"
    sleep 1
done
