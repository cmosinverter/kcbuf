#!/bin/sh

cd /mnt/modules
insmod ringbuf.ko

cd /sys/kernel/tracing

echo 'ring_read'                       >> set_ftrace_filter
echo 'ring_write'                      >> set_ftrace_filter
cat set_ftrace_filter

echo 1 > function_profile_enabled

cd /mnt/modules
./bench

cd /sys/kernel/tracing
echo 0 > function_profile_enabled

echo '===== CPU0 (producer / write) ====='
cat trace_stat/function0
echo '===== CPU1 (consumer / read) ====='
cat trace_stat/function1

echo > set_ftrace_filter

rmmod ringbuf
