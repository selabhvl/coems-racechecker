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
#include <linux_cedar_init.h>
#include <linux_cedar.h>

extern "C" void itm_init()
{
	linux_cedar_init(false,true);
	sleep(1);
	cedar_set_appid(0);
	sleep(1);
}
