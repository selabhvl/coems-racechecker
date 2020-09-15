/*
 * Empty C++ Application
 * Nabbed from coems-toolchain/linux_app/src/main.cc
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <cedar_linux.h>

extern "C" void itm_init()
{
	cedar_linux_init_epu(false,false,0xc01011c4, 0xc010bba4);
	cedar_set_appid(0);
}
