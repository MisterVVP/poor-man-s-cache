#!/bin/bash
set -euo pipefail

echo 'Building all tests...'

pushd ./src > /dev/null
g++ -std=c++23 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ -L/usr/lib/ hash/*.cpp compressor/gzip_compressor.cpp kvs/*.cpp primegen/primegen.cpp -lz -lgtest -lgtest_main -o ../kvs_test
g++ -std=c++23 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ compressor/*.cpp -lz -lgtest -lgtest_main -o ../test_gzip
g++ -std=c++23 -O3 -s -DNDEBUG -pthread -I/usr/include/ -I/usr/local/include/ server/protocol.cpp server/protocol_test.cpp -lgtest -lgtest_main -o ../protocol_test
popd > /dev/null

export NUM_ELEMENTS=10000000
echo 'Running kvs tests...'

./kvs_test

echo 'Running protocol tests...'

./protocol_test

echo 'Running gzip tests...'

./test_gzip

echo 'All tests completed successfully.'
