#!/bin/bash

src/ch-placement-lookup hash_spooky 256 1 100 3
if [ $? -ne 0 ]; then
    exit 1
fi

src/ch-placement-lookup hash_spooky 256 16 100 3
if [ $? -ne 0 ]; then
    exit 1
fi
