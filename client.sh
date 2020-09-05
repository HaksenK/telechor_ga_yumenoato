#!/bin/sh

if [ $# -ne 1 ]; then
  echo "Usage: $0 [IP address]"
  exit 1
fi

SOX_OPTS="--buffer 128"
./client $1 | play -V1 -t raw -b 16 -c 1 -e s -r 44100 --buffer 128 -
