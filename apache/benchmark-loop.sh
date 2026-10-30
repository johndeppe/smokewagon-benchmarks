#!/bin/bash

TOTAL_CORES=$(nproc)

for max in 32 40 48 49 50 51 52 53 54 55 56 57 58 59
do
    # hey gets the cores apache doesn't
    hey=$((TOTAL_CORES - 1 - max))

    for concurrency in 40 50 60 80 100 150 200
    do
        echo "=== max-cores $max, hey-cores $hey, concurrency $concurrency ==="
	#./benchmark.sh --min-cores 1 --max-cores $max --hey-cores $hey --concurrency $concurrency || exit 1
        ./benchmark.sh --min-cores 1 --max-cores $max --hey-cores $hey --concurrency $concurrency --smokewagon off || exit 1
    done
done
