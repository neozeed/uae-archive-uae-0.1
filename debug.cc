 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * debugger v0.0
  * 
  * (c) 1995 Bernd Schmidt
  * 
  */

#include <ctype.h>
#include <iostream.h>
#include <iomanip.h>

#include "amiga.h"
#include "memory.h"
#include "cpu.h"
#include "custom.h"
#include "cia.h"

void ignore_ws(char *&c)
{
  while (isspace(*c)) c++;
}

ULONG readhex(char *&c)
{
  ULONG val = 0;
  char nc;

  ignore_ws(c);

  while (isxdigit(nc = *c++)){
    val *= 16;
    nc = toupper(nc);
    if (isdigit(nc)) {
      val += nc - '0';
    } else {
      val += nc - 'A' + 10;
    }
  }
  return val;
}

char getchar(char *&c)
{
  ignore_ws(c);
  return *c++;
}

bool more_params(char *&c)
{
  ignore_ws(c);
  return (*c) != 0;
}

void dumpmem(CPTR addr,CPTR &nxmem,int lines)
{
  for (;lines--;){
    cout << hex << setfill('0') << setw(8) << addr << " ";
    for(int i=0; i< 16; i++) {
      cout << setw(4) << mem.get_word(addr) << " "; addr += 2;
    }
    cout << "\n";
  }
  nxmem = addr;
}

void debug()
{
  char input[80],c;
  CPTR nextpc,nxdis,nxmem;

  cout << "debugging...\n";
  MC68000_dumpstate(nextpc);
  nxdis = nextpc; nxmem = 0;
  for(;;){
    char cmd,*inptr;
    cout << ">";
    cin.get(input,80,'\n');
    cin.get(c);
    inptr = input;
    cmd = getchar(inptr);
    switch(cmd){
    case 'q': 
      return;
    case 'c':
      dumpcia(); dumpcustom(); break;
    case 'r':
      MC68000_dumpstate(nextpc); break;
    case 'd': 
      {
	ULONG daddr;
	int count;
	
	if (more_params(inptr)) daddr = readhex(inptr); else daddr = nxdis;
	if (more_params(inptr)) count = readhex(inptr); else count = 10;
	MC68000_disasm(daddr,nxdis,count);
      }
      break;
    case 't': 
      MC68000_step(); MC68000_dumpstate(nextpc); break;
    case 'z': 
      MC68000_skip(nextpc); MC68000_dumpstate(nextpc); nxdis = nextpc; break;
    case 'f': 
      MC68000_skip(readhex(inptr)); MC68000_dumpstate(nextpc); break;
    case 'm':
      {
	ULONG maddr; int lines;
	if (more_params(inptr)) maddr = readhex(inptr); else maddr = nxmem;
	if (more_params(inptr)) lines = readhex(inptr); else lines = 16;
	dumpmem(maddr,nxmem,lines);
      }
      break;
    }
  }
}
