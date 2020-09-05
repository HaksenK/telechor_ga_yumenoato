#!/bin/sh

SCRIPT_DIR=$(cd $(dirname $0); pwd)
cd $SCRIPT_DIR
read -p "サーバのIPアドレスを指定: " ip

chmod +x ./client
SOX_OPTS="--buffer 128"
./client $ip | play -V1 -t raw -b 16 -c 1 -e s -r 44100 --buffer 128 -
