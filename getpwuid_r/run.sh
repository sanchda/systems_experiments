#!/bin/bash
# Example.  Collect all active UIDs as root, then pass them into the application 
sudo find /proc -name status -exec awk '/Uid:/ {print $2}' {} \; 2>/dev/null | sort -u | xargs ./test
