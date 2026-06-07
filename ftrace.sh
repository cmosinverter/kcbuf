#!/bin/sh

cd /mnt/modules
insmod ringbuf.ko

cd /sys/kernel/tracing

echo 'mutex_lock_interruptible_nested' >  set_ftrace_filter
echo 'mutex_unlock'                    >> set_ftrace_filter
echo '__mutex_lock'                    >> set_ftrace_filter
echo '__mutex_unlock_slowpath'         >> set_ftrace_filter
echo 'mutex_spin_on_owner'             >> set_ftrace_filter
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
