#!/bin/sh

# Build the project
echo "Building the project..."
make clean
make all
echo

echo "==================================================="
echo "              Performance Test Suite                 "
echo "==================================================="
echo

# Test 1: Cache Performance Test
echo "Test 1: LRU Cache Performance Test"
echo "Description: Evaluating cache performance with different"
echo "file sizes and access patterns (sequential and random)"
echo "==================================================="
echo
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
      
      # Clear system caches
      sync
      echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1

      output=$(./lab2_test $mode $FILE $((size*1024*1024)))

      no_cache=$(echo "$output" | awk -F'[= ]' '{print $2}')
      with_cache=$(echo "$output" | awk -F'[= ]' '{print $5}')

      echo "$size      | $mode |  $run  | $no_cache       | $with_cache"
    done
  done
done

echo
echo "==================================================="
echo "Test 2: External Integer Sorting Test"
echo "Description: Testing the performance of external"
echo "merge sort implementation for integer arrays"
echo "==================================================="
./ema-sort-int-test

# Cleanup section
echo
echo "Cleaning up temporary files..."
rm -f $LOADER_FILE
rm -f run-*-*.bin merge-*-*.bin input.bin output_sys.bin output_lab2.bin
make clean
echo "Cleanup complete"