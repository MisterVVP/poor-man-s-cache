#!/bin/bash

./kvs_test
if [ $? -ne 0 ]; then
 exit $?
fi

./test_gzip

exit $?