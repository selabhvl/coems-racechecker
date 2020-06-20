#!/bin/bash
DR_PATH=`pwd`
bin_file=banker_lock
NTHREADS=2
# Generated TeSSla Spec
OUTFILE=banker.spec

# arch target (x86/ARM)
# "-i" == ARM
# "" == x86
arch=""

# Shared variables
sv="accts[0].balance"
# locks
l="transaction_mtx accts[0].mtx"
i=1;
for (( i=1; i<$NTHREADS; i++ ))
do
      sv="$sv accts[$i].balance";
      l="$l accts[$i].mtx";
done

$DR_PATH/bin/mkDR.sh $arch $bin_file "${sv}" "${l}" > $OUTFILE
