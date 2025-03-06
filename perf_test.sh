#!/bin/sh

make clean
make all

echo "Size(MB) | Mode  | Run | no_cache(ms) | with_cache(ms)"
echo "------------------------------------------------------"

for size in 256 512 1024
do
  FILE="testfile_${size}MB.bin"
  
  dd if=/dev/zero of=$FILE bs=1M count=$size 2>/dev/null
  
  for mode in seq rand
  do
    for run in 1 2 3
    do
      sync
      echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1

      output=$(./lab2_test $mode $FILE $((size*1024*1024)))

      no_cache=$(echo "$output" | awk -F'[= ]' '{print $2}')
      with_cache=$(echo "$output" | awk -F'[= ]' '{print $5}')

      echo "$size      | $mode |  $run  | $no_cache       | $with_cache"
    done
  done
done
