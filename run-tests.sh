#!/bin/bash
set -e

cd build
export LD_LIBRARY_PATH="${PWD}:${LD_LIBRARY_PATH}"

./canfetti_unittest
./canfetti_threadtest
./canfetti_blocktest
./canfetti_vectortest
./canfetti_generationtest
./canfetti_blockmode_edge
