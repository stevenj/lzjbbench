#!/usr/bin/env bash
# Place files to test with in ../test-files/
# recommend "Silesia Corpus" : http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia
# optional: http://mattmahoney.net/dc/textdata.html
#           http://corpus.canterbury.ac.nz/descriptions/
# It is useful to leave the original "Corpus" archives in the test data to test
# compressing uncompress-able data.
#
# Will run benchmarks on ZFS main algorithms for comparison to
# LZJB Compression as used in ZFS
# LZJB Decompression as used in ZFS on linux.
# LZJB Decompression as used in ZFS on bsd.
# New LZJB Decompression routine.
# Each block size from 1K-4M will be tested and the results saved.
# NOTE: ZFS lzjb will not compress with blocks of 512K.
#       Which is why that block size is not tested.

# The process runs and consumes 100% CPU on a single core.
# 4 things will produce the biggest perturbation(s) of the results.
# 1. The Scheduler flicking the process between cores as cache locality is lost.
# 2. The Scheduler prioritizing other tasks over the benchmark, starving it of CPU time.
# 3. The CPU scaling its frequencies up and down.
# 4. Running out of memory.
#
# Fixes for these:
# 1. Use taskset to set processor affinity to lock us to a core and its attendant caches.
CORE="1"
# 2. Use nice to give the highest sheduling priority we can
PRIO="-20"
# 3. This changes the governor to performance, which maximizes the cpu core frequency.
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
echo "Cores are set to:"
for gov in `ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`; do
  scale=`cat ${gov}`
  echo ${gov} = ${scale}
done

# 4. Try not to run anything while the test is underway and have enough memory to hold both the
#    uncompressed and compressed image (*2) in ram.
#
# You may need to run as ROOT for 1,2 and 3 to be effective.
#
# The benchmark attempts to eliminate any IO during the timing so the
# RAW performance for the compressor/de-compressor can be measured.
#
# The ONLY option to this script is the test type.
# Default [No Option] - use the benchmark built with -O3.
# O2 - use the benchmark built with -O2.
# O1 - use the benchmark built with -O.
# 32 - use the 32 bit version of the benchmark.
# -dbg - use the benchmark built for debugging with no optimization.
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B1 -C048 -D0567 ../test-files/* 2> >(tee run-1K.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B2 -C048 -D0567 ../test-files/* 2> >(tee run-4K.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B3 -C048 -D0567 ../test-files/* 2> >(tee run-16K.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B4 -C048 -D0567 ../test-files/* 2> >(tee run-64K.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B5 -C048 -D0567 ../test-files/* 2> >(tee run-256K.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B6 -C048 -D0567 ../test-files/* 2> >(tee run-1M.out >&2)
taskset -c ${CORE} nice -n ${PRIO} ./fullbench$1 -B7 -C048 -D0567 ../test-files/* 2> >(tee run-4M.out >&2)

# Put the governor back to "ondemand" the likely initial default.  Change this if you dont like it.
echo ondemand | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
echo "Cores are re-set to:"
for gov in `ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`; do
  scale=`cat ${gov}`
  echo ${gov} = ${scale}
done
