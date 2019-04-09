 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * (c) 1995 Bernd Schmidt
  */

#include <iostream>
#include <iomanip>
#include <assert.h>

#include "amiga.h"
#include "memory.h"
#include "cpu.h"
#include "disk.h"
#include "debug.h"
#include "xwin.h"

int main(int argc, char **argv)
{
  InitX();
  DISK_init();
  MC68000_init();
  MC68000_reset();
  debug();
}

