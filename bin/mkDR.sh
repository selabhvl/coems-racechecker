#!/bin/bash
set -e

# This script takes names of global shared variables and mutexes,
# and extracts the necessary addresses from the binary with gdb.
# It then instantiates the desired template.
# TODOs:
# * you need to pass at least one shared variable, even if you don't want to

# Option for ITM/HW. Current effects:
# * switch template
# * downcast addresses to 16bit
# * subtract stuck bit coems-tools/coems-toolchain#29
itm=0

# Option to enable generation of input streams for dynamic access monitoring.
# * takes an integer >= 0, byte-range to monitor from base offset
dynamic=""

# end options

cast=""
template="tessla_generator_race_detection_diff_ts.py"

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -h|--help)
       echo 'Usage: mkDR.sh [-d N] [-i] path/to/bin "x y z" "l1 l2"'
       exit 0
       ;;
    -i|--itm)
	itm=1
	echo >&2 "itm & bit15 mitigation enabled"
	echo "-- COEMS: template instantiated with bit15 mitigation"
	cast="(uint16_t)"
	template="tessla_generator_race_detection_diff_ts.py"
	shift # past argument
	;;
    -d|--dynamic)
	dynamic=$2
	echo >&2 "Creating dynamic address slot with range: ${dynamic}."
	shift; shift
        if [[ ! "$dynamic" =~ ^[0-9]+$ ]]; then
          echo >&2 "Option 'dynamic' requires an integer value, but you said: ${dynamic}!"
          exit 1
        else
          # patch up command line for later:
          dynamic="-d $dynamic"
        fi
	;;
    *)    # unknown option
	POSITIONAL+=("$1") # save it in an array for later
	shift # past argument
	;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

LOCKHOME=$(dirname "$BASH_SOURCE")
# Overwritte by `make install`:
TEMPLATEDIR="$LOCKHOME"/../tessla_patterns/eraser_algorithm
binary=$1
sv=$2
ls=$3

if [ -z "$binary" ]; then echo >&2 "Missing mandatory arguments (binary, shared variables, locks)!"; exit 1; fi
if [ -z "$sv" ]; then echo >&2 "Missing mandatory arguments (shared variables, locks)!"; exit 1; fi
if [ -z "$ls" ]; then echo >&2 "Missing mandatory arguments (locks)!"; exit 1; fi
if [ ! -z $4 ]; then echo >&2 "Excess arguments!"; exit 1; fi
if [ ! -r "$binary" ]; then echo >&2 "Binary $binary not readable!"; exit 1; fi
if [[ ! -z "$cast" ]] && ! (file "$binary" | grep -q "ELF 32-bit LSB executable, ARM, EABI5"); then
  echo >&2 "*** -i option only applies to ARM binaries!"; exit 1
fi


# local vars:
s=""
l=""
addrtableV="-- "
for v in $sv; do
    val=`gdb -batch -ex "p/u $cast&$v" $binary | cut -f 3 -d ' '`
    if [ $itm -eq 1 ]; then
	val=$(( $val & 2#0111111111111111))
    fi
    addrtableV="$addrtableV $v:$val"
    s="$s $val"
done
addrtableL="-- "
for v in $ls; do
    val=`gdb -batch -ex "p/u $cast&$v" $binary | cut -f 3 -d ' '`
    if [ $itm -eq 1 ]; then
	val=$(( $val & 2#0111111111111111))
    fi
    addrtableL="$addrtableL $v:$val"
    l="$l $val"
done

echo "-- addresses:"
echo $addrtableV
echo $addrtableL
# Recall that lock-addr 1 is virtual & hard-coded for the main-thread.
python $TEMPLATEDIR/$template $dynamic -s "$s" -l "1 $l"
