 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * memory management v0.1
  * v0.0: 950225 four simple functions to get/put bytes/words
  * v0.1: 950228 made banks classes instead of 64k arrays to support
  *              custom chips as well
  *
  * (c) 1995 Bernd Schmidt
  */

#include <iostream>
using namespace std;
#include <fstream>

#include "amiga.h"
#include "memory.h"

const char romfile[] = "kick.rom"; 

UWORD ram64k::wget(UWORD addr)
{
  return ram[addr>>1];
}

UBYTE ram64k::bget(UWORD addr)
{
  return ram[addr>>1] >> (addr & 1 ? 0 : 8);
}

void ram64k::wput(UWORD addr,UWORD value)
{
  ram[addr>>1] = value;
}

void ram64k::bput(UWORD addr,UBYTE value)
{
  if (!(addr & 1)) {
    ram[addr>>1] = (ram[addr>>1] & 0xff) | (((UWORD)value) << 8);
  } else {
    ram[addr>>1] = (ram[addr>>1] & 0xff00) | value;
  }
}

class rom64k: public ram64k {
 public:
  rom64k(ifstream &);
  virtual void wput(UWORD addr, UWORD value) { /* READ ONLY */ };
  virtual void bput(UWORD addr, UBYTE value) { /* READ ONLY */ };
};

rom64k::rom64k(ifstream &romin)
{
  for (int i=0; i<32768; i++){
    UWORD data = romin.get();
    data = (data << 8) | romin.get();
    ram[i] = data;
  }
}

amigamemory mem;

amigamemory::amigamemory()
{
  // init chipmem
  
  int bank = 0;
  for(int i=0;i<chipmem_size;i+=64){
    banks[bank++] = new ram64k; // allocate chipmem banks
    cout << "got 64k chipmem\n";
  }
  for(int b2=bank;b2<256;b2++) {      // mirror the chipram everywhere, so
    banks[b2] = banks[b2 % bank]; // that we don't have holes
  }

  // init rom
  ifstream romin(romfile,ios::in | ios::binary);
  for (bank = 0;bank<8;bank++){
    cout << "got 64k rom\n";
    banks[248+bank] = new rom64k(romin);
  }
  // init custom chips
  banks[192] = new custom64k;
  for (bank = 193; bank < 224; bank++) {
    banks[bank] = banks[192];
  }
  // init CIA
  banks[191] = new cia64k; 
  cout << "Memory initialized OK\n";
}

amigamemory::~amigamemory()
{
  
}

