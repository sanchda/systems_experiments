#!/bin/bash

output_file="output.txt"
rm -rf ${output_file}

# Build once just to print the header
gcc-11 -o test test.c
./test header > ${output_file}

# Now harvest some data
for march in $(gcc-11 -Q --help=target | grep -A1 'mtune= option' | tail -n 1); do
    gcc-11 -O3 -mtune=${march} -DMTUNE=\"${march}\" -o test test.c > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Compilation successful for -mtune=${march}"
    else
        echo "Compilation failed for -mtune=${march}"
        continue
    fi

    ./test > tmp_output.txt
    if [ $? -eq 0 ]; then
        cat tmp_output.txt >> ${output_file}
    fi
done
rm -rf tmp_output.txt
