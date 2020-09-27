# COEMS Dynamic Data Race Checker

https://www.coems.eu/coems-lock-instrumentation-tool/

# About

The tool `bin/instrument` instruments LLVM intermediate code to introduce the necessary event-generation for the various runtime analyses, such as data race detection.
Further shell-scripts and Makefiles provide a sample of possible integration to conduct full runs using ITM tracing.

# Options for the `instrument` binary:

`instrument` has many legacy options whose effect is unclear. Handle with care.

`-nt/--normalize-timestamps`: timestamps start from zero instead of a wall-clock derived value.
Essential to surpress timestamp overflows due to our casual handling of 64bit counters.
Always recommended to be `on`, this may become the new default.

`--no-varname-resolution`: for more readable software-traces, we try to obtain any variable names from the debug info. However, this code has a known issue where e.g. optimized code will send it into an infinite loop (also see below). This option should avoid this problem.

# Software trace

The following section describes the details of tracing facility. If your purpose is data race checking, skip ahead.

## Output:

Execution of the instrumented program will output the following events to the log file:

    instruction  function  line  column  threadid  functioncall  pcreate readvar
    writevar  varaddr  varsize varoffset mutexlockid mutexlockini mutexlock mutexunlock

Every event will contain the following data:

	instruction : LLVM instruction executed, e.g. "call", "load", "store";
	function : name of the function the instruction located;
	line : line no. in source code;
	column : column no. in source code;
	threadid : thread ID of the instruction executing in;

Depending on the type of event, the following additional data is available:

    functioncall : if the instruction is a "call", then the function name it called to;

Variable accesses:

	readvar : name of the variable read from;
  	writevar : name of the variable read to;
	varaddr : address of the current read or write;
	varsize : bytes of the current read or write, e.g. 4 bytes for integer in X64 machine;
	varoffset : bytes from the current read or write position to the start of the variable

E.g., for a declared variable `int x[5]`, if varaddr=2088, varsize = 4, varoffset = 8, that means `x[2]` is accessed, and the variable `x` is starting from address 2080 in memory;

Threading API:

    pcreate : name of the pthread if function call to "pthread_create";
    mutexlock : name of the lock just locked if statically available;
    mutexlockaddr : adress of the lock structure just locked;
    mutexunlock : name of the lock just unlocked if statically available;
    mutexunlockaddr : adress of the lock structure just unlocked;
    mutexlockid : mutex lock ID while initializing, locking and unlocking by calling pthread_mutex_init, pthread_mutex_lock and pthread_mutex_unlock;
    mutexlockini : name of the lock initialized;

# Dependencies

You need the basic tools to build C++ projects (make, g++, etc.) as well as the LLVM libraries and the Boost program-options library. For COEMS hardware-tracing, you need the Xilinx SDK as well as the `coems-tools/coems-toolchain` project checked out. To run the examples you also need `clang` to generate LLVM-bitcode from the example C code.
`jq` is required to process intermediate files. For both hardware- and software tracing you will need the TeSSLa interpreter from https://www.tessla.io/.

You can install all required tools and libraries using the following command:

    apt-get update
    apt-get install build-essential libboost-program-options-dev clang-6.0 llvm-6.0 llvm-6.0-tools gdb openjdk-11-jre-headless jq

Note that the aim is to support more recent versions of LLVM, but this may require manual intervention
on your particular platform, see https://apt.llvm.org. Also take a look at the Dockerfile for the
top-level `llvm-static` project which contains an exact receipe. We're in addition limited to the software installed on DICE in Dresden.

# Build

For **software-only instrumentation**, invoke `make bin/libinstrumentation_local.a`.

For **hardware**-instrumentation, you need a copy of the `coems-tools/coems-toolchain` project checked out.
You also need Xilinx SDK 2018.2 from the Xilinx archives, which is the version that is used on the DICE system.
You then need to configure the `Makefile` correctly with those paths, e.g.

    $ make COEMSTC=/home/user/git/coems-toolchain

As some of the subsequent tooling also requires this project, it's probably best to

    $ export COEMSTC=/home/user/git/coems-toolchain.

The executable file will be created in the `bin` directory, as will the required `.a` runtime support for writing the trace or hw-tracing via ITM.
The Makefile above and the script below contain various variables to influence the choice
of LLVM & Clang in case there are several on the system.

# Generating Traces

## Traces via SW-tracing

Run an example in the "examples" directory, such as counting.c :

    $ bash ./run_one.sh examples/counting.c

The compiled binary will be in the current directory. The output trace as file "traces.log" will be in the
directory from which you started the binary.

The output file can be adjusted through use of the environment
variable `COEMSTRACE`.

## Traces with HW-tracing using ITM

Invoke the script as `run_one.sh -c -a ...`. This will cross-compile the program for ARM with ITM support and not execute it automatically. See section `Arm experiment` below for a full walk-through.

## Unit tests

The `tests` directory contains several Makefiles with multiple targets for unit testing.
They will compile some examples, run them, and feed the sw-trace through TeSSLa to check for correct/expected results.
Note that some particular code patterns produce known false-positives.

# Data race specifications

The TeSSLa specification for data race detection (lockset algorithm) can be generated from the templates. The only supported concurrency construct are `pthread_mutex`.

There are several TeSSLa templates in `data-race-spec` (those marked with † are for older versions of TeSSLa):
* template_hw_sw_diff_ts.tessla (D4.2, TeSSLa 1.0.7, almost the same as template_hw_sw.tessla,
  should be used if events on threadid appear before access and lock events, as they do in hardware-traces.
  You can use `diff_ts.pl` to massage an sw-trace (where multiple data is produced at the same point in time) that works for `template_hw_sw.tessla` into one for the hardware (where each datum will have its own timestamp).
  Older TeSSLa 0.5.0 - 0.6.5 in [race_validation](hvl/coems-toolchain/xsdk_projects/race_validation/epu/))
* full_lockset_tessla_$VER_varaddr.tessla (D4.2 and later, the basic version of lockset
  with data structures (hence sw-only), does not need any parameters,
  checking starts after the first `pthread_create()`)

The templates take different parameters; as the templates are scripts, you can easily inspect them if in doubt.

### full_lockset_tessla

* no parameters required, sw-trace only.

### tessla_generator_race_detection.py / tessla_generator_race_detection_diff_ts.py

* list of memory locations holding locks used in the program,
* list of memory locations to be checked.

For example, to generate a specification that checks accesses to addresses `42` and `48` in a program which uses locks at addresses `124` and `132`:

    $ python tessla_generator_race_detection_diff_ts.py -l "124 132" -s "42 48"

Note the significant use of quotes. See the various Makefiles such as `epu/Makefile` for how to script `gdb` to extract those addresses for global variables.

# Arm experiment

The `epu`-folder contains a full example. First, run `make all`, which will do the following steps:

- cross-compile the hardcoded `example.c` through the `run_one.sh` script with the hw-instrumentation
- instantiate the data race template with the addresses of the hardcoded shared variable "x" and lock "m". The addresses are extracted via `gdb`,
note that we directly include the cast to `uint16_t` there. If using a different debugger, keep in mind that `./example` is cross-compiled for ARM.
- split the spec into the first part that fits into the COEMS-box, and the remainder for post-processing with sw-TeSSLa
- compile the first part for the CEDAR-box using the compiler from the `coems-toolchain`-project
- the Makefile will print the magic number `COEMSEPUS=NNNNNNNNNNNN` that we need later. These are the EPU id/command pairs corresponding to the input streams that may change on recompilation of your spec, and they will be used at runtime to emit the ITM events to the right EPUs. Note that there is not (cannot be) any validation; wrong values for your spec will give garbage results.

Now for the hardware-side of things. Be sure the multiplex your terminal, as you need to have a shell open into the Enclustra-board, as well as to DICE.

- `scp example root@enclustra:`
- configure EPUs and start tracing using the `run_demo.sh` script
- invoke `COEMSEPUS=NNNNNNNNNNNN ./example` on the Eclustra board
- output should show up from the trace; the example program may print some irrelevant debugging output

# Limitations

* `make install` has not been tested in a long, long while (issue #39).
* be aware that the abstraction into 16 (15) bits for the ITM payload can easily give false results when consuming those values from the trace.
* our 64-bit timestamps currently quickly run over and TeSSLa will throw `Decreasing time stamps`. Always use `instrument -n` for now! (issue #54)
* sw-trace too verbose, invoke `instrument -p` to only include events useful for race checking.
* pthread-calls through function pointers are not traced, we would need LD_PRELOAD for that. Example:
```
fp = &pthread_mutex_lock();
(fp*)(&lock) // not traced!
```
* expected glitching: the ITM/logging-code is not atomic with the pthread-operations. That means there is a very small time-window i) after taking a lock, ii) before releasing a lock, where the trace does not accurately reflect the state of the system. However, any of the resulting possible interleavings are equivalent for the purpose of race checking.
