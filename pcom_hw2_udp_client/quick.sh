#!/bin/bash

# Loop 30 times
for i in {1..50}
do
    # Call the Python script
    python3 udp_client.py 127.0.0.1 1236

done
# use this for fast commands in subscriber
# cat a.txt - | ./client