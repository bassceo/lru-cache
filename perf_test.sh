#!/bin/sh

make clean
make all

# echo "Size(MB) | Mode  | Run | no_cache(ms) | with_cache(ms)"
# echo "------------------------------------------------------"

# for size in 256 512 1024
# do
#   FILE="testfile_${size}MB.bin"
  
#   dd if=/dev/zero of=$FILE bs=1M count=$size 2>/dev/null
  
#   for mode in seq rand
#   do
#     for run in 1 2 3
#     do
#       sync
#       echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1

#       output=$(./lab2_test $mode $FILE $((size*1024*1024)))

#       no_cache=$(echo "$output" | awk -F'[= ]' '{print $2}')
#       with_cache=$(echo "$output" | awk -F'[= ]' '{print $5}')

#       echo "$size      | $mode |  $run  | $no_cache       | $with_cache"
#     done
#   done
# done

SIZE=4
LOADER_FILE="loader_testfile_${SIZE}MB.bin"

# Создадим тестовый файл:
dd if=/dev/zero of=$LOADER_FILE bs=1M count=$SIZE 2>/dev/null

echo
echo "----------- Loader test (file=${LOADER_FILE}) -----------"
echo "Run | no_cache(ms) | with_cache(ms)"
echo "------------------------------------"

for run in 1 2 3
do
  # Сбросим системный кэш перед запуском
  sync
  echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1

  # Засечём время без пользовательского кэша
  start=$(date +%s.%N)
  ./loader_no_cache "$LOADER_FILE"
  end=$(date +%s.%N)
  nocache_time=$(echo "($end - $start) * 1000" | bc | awk '{printf "%.0f", $0}')

  # Сбросим кэш ещё раз
  sync
  echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1

  # Засечём время с пользовательским кэшем
  start=$(date +%s.%N)
  ./loader_with_cache "$LOADER_FILE"
  end=$(date +%s.%N)
  cache_time=$(echo "($end - $start) * 1000" | bc | awk '{printf "%.0f", $0}')

  echo "$run   | $nocache_time       | $cache_time"
done