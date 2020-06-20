#!/bin/bash
set -e

compileonly=0
armdran=0

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -c|--compileonly)
	compileonly=1
	shift # past argument
	;;
    -a|--armdran) # ARM Dynamic Runtime cedAr iNstrumentation
	armdran=1
	shift
	;;
    *)    # unknown option
	POSITIONAL+=("$1") # save it in an array for later
	shift # past argument
	;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

OPT="${OPT:=opt-6.0}"
CLANG="${CLANG:=clang-6.0}"
LLVMLINK="${LLVMLINK:=llvm-link-6.0}"
LLVMDIS="${LLVMDIS:=llvm-dis-6.0}"

COEMSTC="${COEMSTC:=/home/volker/git/coems-toolchain}"
LOCKHOME=$(dirname "$BASH_SOURCE")
LIBDIR="$LOCKHOME/bin"
BINDIR="$LOCKHOME/bin"

MYTMP=`mktemp -d`
trap "{ rm -rf $MYTMP; }" EXIT

if [ $armdran -eq 1 ]; then
  ARM_FLAGS="--target=arm-linux-gnueabihf --gcc-toolchain=/opt/Xilinx/SDK/2018.2/gnu/aarch32/lin/gcc-arm-linux-gnueabi --sysroot=/opt/Xilinx/SDK/2018.2/gnu/aarch32/lin/gcc-arm-linux-gnueabi/arm-linux-gnueabihf/libc -mcpu=cortex-a9"
else
  ARM_FLAGS=""
fi

# Execute with the name of the example, for example: ./run_one.sh examples/counting.c
# Will create instrumented binary in the current directory.
# To do: clean up tmp-files? Don't clobber traces.log?
if [ -z $1 ]; then exit 1; fi
if [ $armdran -eq 1 ] && [ ! -f "$COEMSTC/lib/libdebugARM_linux.a" ]; then
  echo "\$COEMSTC pointing to the right place? Can't find" $COEMSTC/lib/libdebugARM_linux.a
  exit 1
fi
if [ $armdran -eq 1 ] && [ ! -f "$LIBDIR/libinstrumentation.a" ]; then
  echo "You didn't \"make\" yet? libinstrumentation.a is missing!"
  exit 1
else
  if [ ! -f "$LIBDIR/libinstrumentation_local.a" ]; then
    echo "You didn't \"make\" yet? libinstrumentation_local.a is missing!"
    exit 1
  fi
fi
f=$(basename $1)

if [[ $f == *.c ]]; then
    out=$(basename -s .c $1)
    bc="$out".bc
    $CLANG $CFLAGS $ARM_FLAGS -I. -ggdb -Wall -pedantic -c -emit-llvm $1 -o "$MYTMP/$bc"
else if [[ $1 == *.bc ]]; then
	 # TODO: Since 'instrument' rewrites in place, you don't want to
	 # call this script repeatedly on the same .bc...
	 out=$(basename -s .bc $1);
         cp -p $1 "$MYTMP"
         bc=$f
     else
	 echo "Unknown input format: " $f
	 exit 1
     fi
fi
# The following line is for debugging only; it generates the human-readble IR as well:
# Don't complain if llvm-dis is missing.
if [ $(command -v $LLVMDIS) ]; then
  $LLVMDIS "$MYTMP/$bc" -o "$MYTMP/$f.ll"
fi
# .iout is the instrumentation log
$BINDIR/instrument $EXTRA_I_FLAGS -n --flush -p "$MYTMP/$bc" >"$MYTMP/$f.iout"
# We compile with -fno-pie currently to avoid ASLR, which is the standard on MacOS.
# It is not necessary on (our) Linux x86, and of course there is always the option of
#  switching this off system-wide at run-time instead for compilation.
$CLANG -c $ARM_FLAGS $EXTRA_FINAL_FLAGS -g -fno-pie -pthread "$MYTMP/$bc" -o "$out".o
if [ $armdran -eq 1 ]; then
  /opt/Xilinx/SDK/2018.2/gnu/aarch32/lin/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf-g++ -L"$COEMSTC/lib" -L"$LIBDIR" -static -o "$out" "$out.o" -linstrumentation -ldebugARM_linux -ldebugCore_linux -lpthread
else
  $CLANG -L"$LIBDIR" -static -o "$out" "$out.o" -linstrumentation_local -lpthread
fi
# You can use COEMSTRACE=/path/to/trace.out to change the output filename:
if [ $compileonly -eq 0 ]; then
    ./$out
fi

exit 0
