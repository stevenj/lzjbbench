lzjbbench
=========

LZJB (from ZFS) benchmarking code. vs LZ4 test suite.

Includes experimental lzjb decompressor that is at least 150% the speed of stock lzjb decompression

This code is HEAVILY based on the lz4-r107 release. https://code.google.com/p/lz4/

I took that code, and the lzjb code from zfs and added the ability for it
to benchmark the zfs lzjb compressor/decompresser.

It aims to work completely in memory to eliminate IO vagaries which would
perturb the result.  Accordingly it hopes to approach the RAW native CPU bound
speed of each algorithm

The LZ4 code has exactly the same benchmarking overheads as the lzjb code and
so it is as close to an "apples for apples" comparison as its reasonable to make.

Both LZJB and LZ4 benchmarking will suffer from:

  1. The Scheduler flicking the process between cores as cache locality is lost.
  2. The Scheduler prioritizing other tasks over the benchmark, starving it of CPU time.
  3. The CPU scaling its frequencies up and down.
  4. Running out of memory.

The script test_run.sh attempts to ameliorate 1-3 in the above list by locking
the benchmark to a single core, and increasing its priority as much as possible,
and making the cpu use the "performance" governor.

compiling
=========

$ make all

Should be all you need to do.  It will build all the standard lz4 code, plus:

: fullbench
: fullbenchO2
: fullbenchO1
: fullbench-dbg
: fullbench32

using
=====

These are modified versions of the LZ4 fullbench program.
$ fullbench -H
will give you the command line.
Notable Options:
    -C will let you pick the compressors used individually.
        0 = LZ4 Fast
        4 = LZ4HC
        8 = LZJB as used by ZFS on Linux.
    -D will let you pick the decompressors used individually.
        0 = LZ4
        5 = LZJB as used by ZFS on Linux.
        6 = LZJB as used by BSD.
        7 = Experimental High(er)speed LZJB decompressor.

    -B will let you select the compression block size. (1-7)
        this selects a block between 1K (1) and 4M (7)
        0 will select a block of 512, however lzjb does not seem to produce
        a valid compressed bit stream with a buffer of 512.
        I believe it has to do with the LEMPEL_SIZE constant. But i have
        done no further testing on it and do not know for sure.

Notable changes. To help debugging compressors and de-compressors if (when?)
errors occur the benchmark will try and produce a hexdump of the incorrect
data to aid in debugging.

test script
===========

The test script expects test files to be located in ../test-files/

Tests that I have used are:

    * "Silesia Corpus" : http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia
    * http://mattmahoney.net/dc/textdata.html
    * http://corpus.canterbury.ac.nz/descriptions/

I also found it useful to leave the original "Corpus" archives in the test
data to test compressing uncompress-able data.

The test script will automatically run benchmarks on ZFS main algorithms for comparison to :

    . LZJB Compression as used in ZFS
    . LZJB Decompression as used in ZFS on linux.
    . LZJB Decompression as used in ZFS on bsd.
    . Experimental LZJB Decompression routine.
    . Each block size from 1K-4M will be tested and the results saved.

future
======

I would like to try and produce an experimental high(er) speed compressor.  I believe
significant improvements can be made in this area.

I also believe the Experimental LZJB decompresser may be able to be made slightly faster.

I would also like to clean up the decompresser and get it inside ZFS either as an alternative
selectable decompresser for LZJB , if not as a full replacement.

Steven Johnson (Strontium) - October 2013.
