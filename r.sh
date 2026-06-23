#!/bin/sh
sudo apt update
sudo apt install -y gcc make libraylib-dev
make clean
make milestone3
./sim sample_graph.txt
