CXX	= clang++-6.0
ARMCXX	= /opt/Xilinx/SDK/2018.2/gnu/aarch32/lin/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf-g++
ARMCC	= /opt/Xilinx/SDK/2018.2/gnu/aarch32/lin/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf-gcc
CC	= clang-6.0
AR	= ar
LLVMCONFIG	= llvm-config-6.0
COEMSTC		?= /home/volker/git/coems-toolchain
EPUC		= java -jar $(COEMSTC)/bin/isp/epu-compiler-assembly-1.0.2.jar

GIT_VERSION := "$(shell git describe --abbrev=4 --dirty --always --tags)"

.PHONY:	clean all install

all: bin/instrument bin/libinstrumentation_local.a # bin/libinstrumentation_local.a

bin/maptoCheck: maptoCheck.cpp
	@mkdir -p bin
	$(CXX) $< -o $@

bin/instrument: instrument.cc instrumentation.h worklist2.cpp st_utilize.cpp
	@mkdir -p bin
	$(CXX) -DVERSION=\"$(GIT_VERSION)\" `$(LLVMCONFIG) --cxxflags --ldflags --system-libs` -UNDEBUG -Wno-unknown-warning-option -fexceptions -lboost_program_options -Wall -pedantic -g -O2 -std=c++11 $< `$(LLVMCONFIG) --libs` -o $@

instrumentation.o: instrumentation.c instrumentation.h
	$(ARMCC) -DVERSION=\"$(GIT_VERSION)\" -ggdb -Wall -pedantic -fPIC -pthread -DHAVE_COEMS -DWANT_IO -c $< -o $@
l_instrumentation.o: instrumentation.c instrumentation.h
	$(CC) -DVERSION=\"$(GIT_VERSION)\" -ggdb -Wall -pedantic -fPIC -pthread -DARM_AB -DWANT_IO -c $< -o $@

linux_cedar.o: $(COEMSTC)/xsdk_projects/shared/linux_cedar.cpp
	$(ARMCC) -c $< -o $@
linux_cedar_init_zynq.o: $(COEMSTC)/xsdk_projects/shared/linux_cedar_init_zynq.cpp
	$(ARMCC) -c -I$(COEMSTC)/include $< -o $@

itm.o: itm.cc $(COEMSTC)/xsdk_projects/shared/linux_cedar.h
	$(ARMCXX) -ggdb -Wall -pedantic -fPIC -pthread -DHAVE_COEMS -I$(COEMSTC)/include -I$(COEMSTC)/xsdk_projects/shared -c $< -o $@

bin/libinstrumentation.a:	instrumentation.o itm.o linux_cedar.o linux_cedar_init_zynq.o
	@mkdir -p bin
	$(AR) rcs $@ $^

bin/libinstrumentation_local.a:	l_instrumentation.o
	@mkdir -p bin
	$(AR) rcs $@ $^

clean:
	@rm -f l_instrumentation.o instrumentation.o bin/libinstrumentation_local.a bin/libinstrumentation.a bin/instrument linux_cedar.o linux_cedar_init_zynq.o

PREFIX = /usr/local	# where we put it NOW;
INSTALLDIR = $(PREFIX)  # where it thinks it is later.

# Install into PREFIX, avoiding name clashes for now.
install: bin/instrument bin/libinstrumentation.a bin/libinstrumentation_local.a
	install -d ${PREFIX}/bin
	install -d ${PREFIX}/lib
	install bin/instrument ${PREFIX}/bin/lock_instrument
	install bin/libinstrumentation.a ${PREFIX}/lib/liblock_instrument.a
	install bin/libinstrumentation_local.a ${PREFIX}/lib/liblock_instrument_local.a
		  # TODO: Sloppy hack, we really shouldn't do it this way on production systems:
	install data-race-spec/template.tessla data-race-spec/template_hw_sw_diff_ts.tessla data-race-spec/template_hw_sw.tessla ${PREFIX}/bin
	install data-race-spec/tessla.jar ${PREFIX}/lib
	sed -e s+BINDIR=.*$$+BINDIR=${INSTALLDIR}/bin+ \
				-e s+LIBDIR=.*$$+LIBDIR=${INSTALLDIR}/lib+ \
				-e s+libinstrumentation.a+liblock_instrument.a+ \
				-e s+/instrument\ +/lock_instrument\ + \
				-e s+/instrument_local\ +/lock_instrument_local\ + \
				run_one.sh >${PREFIX}/bin/coems_instrument_and_compile.sh
	sed -e s+TEMPLATEDIR.*$$+TEMPLATEDIR=${INSTALLDIR}/bin+ \
	  bin/mkDR.sh >${PREFIX}/bin/coems_mkDR.sh
	@chmod a+rx ${PREFIX}/bin/coems_instrument_and_compile.sh ${PREFIX}/bin/coems_mkDR.sh

