#!/bin/bash
set -u

i=4

gcc -g -std=c99 -Wall -c fs.c -o bin/fs.o &>> log/gcc.log
gcc -g -std=c99 -Wall -I. tests/test$i.c bin/fs.o -o bin/test$i &>> log/gcc.log
if [ ! -x bin/test$i ] ; then
    echo "[$i] compilation error"
    exit 1 ;
fi

if ! ./bin/test$i > log/test$i.out 2> log/test$i.err ; then
    echo "[$i] error"
    exit 1
fi

rm -f bin/test$i log/test$i.out log/test$i.err
exit 0
