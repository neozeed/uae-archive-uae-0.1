 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * CIA chip support v0.1
  * v0.0: 950301 
  *       stubs
  * v0.1: 950304
  *       hopefully all the timer functionality is implemented correctly
  *
  * (c) 1995 Bernd Schmidt
  */

#include <iostream>
using namespace std;
#include <iomanip>

#include "amiga.h"
#include "memory.h"
#include "disk.h"

static UBYTE ciaaicr,ciaaimask,ciabicr,ciabimask;
static UBYTE ciaacra,ciaacrb,ciabcra,ciabcrb;
static UWORD ciaata,ciaatb,ciabta,ciabtb;
static UWORD ciaala,ciaalb,ciabla,ciablb;
static ULONG ciaatod,ciabtod,ciaatol,ciabtol,ciaaalarm,ciabalarm;
static bool ciaatlatch,ciabtlatch;
static UBYTE ciaapra,ciaaprb,ciaadra,ciaadrb,ciaasdr;
static UBYTE ciabpra,ciabprb,ciabdra,ciabdrb,ciabsdr; 
static int div10,hsync,vsync;

static void setclr(UBYTE &p, UBYTE val)
{
  cout << "setclr\n";
  if (val & 0x80) {
    p |= val & 0x7F;
  } else {
    p &= ~val;
  }
}

static void RethinkICRA()
{
  if (ciaaimask & ciaaicr) {
    ciaaicr |= 0x80;
    mem.put_word(0xDFF09C,0x8008);
    cout << "CIA A int\n";
  } else {
    ciaaicr &= 0x7F;
    mem.put_word(0xDFF09C,0x0008);
  }
}

static void RethinkICRB()
{
  if (ciabimask & ciabicr) {
    ciabicr |= 0x80;
    mem.put_word(0xDFF09C,0xA000);
    cout << "CIA B int\n";
  } else {
    ciabicr &= 0x7F;
    mem.put_word(0xDFF09C,0x2000);
  }
}

UBYTE ReadCIAA(UWORD addr)
{
  UBYTE tmp;

  switch(addr & 0xf){
  case 0: 
    return (DISK_status() & 0x3C) | 0xc0; // no mouse
  case 1:
    return ciaaprb;
  case 2:
    return ciaadra;
  case 3:
    return ciaadrb;
  case 4:
    return ciaata & 0xff;
  case 5:
    return ciaata >> 8;
  case 6:
    return ciaatb & 0xff;
  case 7:
    return ciaatb >> 8;
  case 8:
    if (ciaatlatch) {
      ciaatlatch = 0;
      return ciaatol & 0xff;
    } else return ciaatod & 0xff;
  case 9:
    if (ciaatlatch) return (ciaatol >> 8) & 0xff;
    else return (ciaatod >> 8) & 0xff;
  case 10:
    ciaatlatch = 1; ciaatol = ciaatod; // ??? only if not already latched?
    return (ciaatol >> 16) & 0xff;
  case 12:
    return ciaasdr;
  case 13:
    tmp = ciaaicr; ciaaicr = 0; RethinkICRA(); return tmp;
  case 14:
    return ciaacra;
  case 15:
    return ciaacrb;
  }
  return 0;
}

UBYTE ReadCIAB(UWORD addr)
{
  UBYTE tmp;

  switch(addr & 0xf){
  case 0: 
    return ciabpra;
  case 1:
    return ciabprb;
  case 2:
    return ciabdra;
  case 3:
    return ciabdrb;
  case 4:
    return ciabta & 0xff;
  case 5:
    return ciabta >> 8;
  case 6:
    return ciabtb & 0xff;
  case 7:
    return ciabtb >> 8;
  case 8:
    if (ciabtlatch) {
      ciabtlatch = 0;
      return ciabtol & 0xff;
    } else return ciabtod & 0xff;
  case 9:
    if (ciabtlatch) return (ciabtol >> 8) & 0xff;
    else return (ciabtod >> 8) & 0xff;
  case 10:
    ciabtlatch = 1; ciabtol = ciabtod;
    return (ciabtol >> 16) & 0xff;
  case 12:
    return ciabsdr;
  case 13:
    tmp = ciabicr; ciabicr = 0; RethinkICRB(); return tmp;
  case 14:
    return ciabcra;
  case 15:
    return ciabcrb;
  }
  return 0;
}

void WriteCIAA(UWORD addr,UBYTE val)
{
  switch(addr & 0xf){
  case 0: 
    ciaapra = (ciaapra & ~0x3) | (val & 0x3); break;
  case 1:
    ciaaprb = val; break;
  case 2:
    ciaadra = val; break;
  case 3:
    ciaadrb = val; break;
  case 4:
    ciaala = (ciaala & 0xff00) | val;
    break;
  case 5:
    ciaala = (ciaala & 0xff) | (val << 8);
    if ((ciaacra & 1) == 0) ciaata = ciaala;
    if (ciaacra & 8) { ciaata = ciaala; ciaacra |= 1; }//??? load latch always?
    break;
  case 6:
    ciaalb = (ciaalb & 0xff00) | val;
    break;
  case 7:
    ciaalb = (ciaalb & 0xff) | (val << 8);
    if ((ciaacrb & 1) == 0) ciaatb = ciaalb;
    if (ciaacrb & 8) { ciaatb = ciaalb; ciaacrb |= 1; }
    break;
  case 8:
    if (ciaacrb & 0x80){
      ciaaalarm = (ciaaalarm & ~0xff) | val;
    } else {
      ciaatod = (ciaatod & ~0xff) | val;
    }
    break;
  case 9:
    if (ciaacrb & 0x80){
      ciaaalarm = (ciaaalarm & ~0xff00) | (val << 8);
    } else {
      ciaatod = (ciaatod & ~0xff00) | (val << 8);
    }
    break;
  case 10:
    if (ciaacrb & 0x80){
      ciaaalarm = (ciaaalarm & ~0xff0000) | (val << 16);
    } else {
      ciaatod = (ciaatod & ~0xff0000) | (val << 16);
    }
    break;
  case 12:
    ciaasdr = val; break;
  case 13:
    setclr(ciaaimask,val); break; // ??? call RethinkICR() ?
  case 14:
    ciaacra = val;
    if (ciaacra & 0x10){
      ciaacra &= ~0x10;
      ciaata = ciaala;
    }
    cout << "CIAACRA write" << val << "\n";
    break;
  case 15:
    ciaacrb = val; 
    if (ciaacrb & 0x10){
      ciaacrb &= ~0x10;
      ciaatb = ciaalb;
    }
    cout << "CIAACRB write" << val << "\n";
    break;
  }
}

void WriteCIAB(UWORD addr,UBYTE val)
{
  switch(addr & 0xf){
  case 0:
    ciabpra = (ciabpra & ~0x3) | (val & 0x3); break;
  case 1:
    ciabprb = val; DISK_select(val); break;
  case 2:
    ciabdra = val; break;
  case 3:
    ciabdrb = val; break;
  case 4:
    ciabla = (ciabla & 0xff00) | val;
    break;
  case 5:
    ciabla = (ciabla & 0xff) | (val << 8);
    if ((ciabcra & 1) == 0) ciabta = ciabla;
    if (ciabcra & 8) { ciabta = ciabla; ciabcra |= 1; } 
    break;
  case 6:
    ciablb = (ciablb & 0xff00) | val;
    break;
  case 7:
    ciablb = (ciablb & 0xff) | (val << 8);
    if ((ciabcrb & 1) == 0) ciabtb = ciablb;
    if (ciabcrb & 8) { ciabtb = ciablb; ciabcrb |= 1; }
    break;
  case 8:
    if (ciabcrb & 0x80){
      ciabalarm = (ciabalarm & ~0xff) | val;
    } else {
      ciabtod = (ciabtod & ~0xff) | val;
    }
    break;
  case 9:
    if (ciabcrb & 0x80){
      ciabalarm = (ciabalarm & ~0xff00) | (val << 8);
    } else {
      ciabtod = (ciabtod & ~0xff00) | (val << 8);
    }
    break;
  case 10:
    if (ciabcrb & 0x80){
      ciabalarm = (ciabalarm & ~0xff0000) | (val << 16);
    } else {
      ciabtod = (ciabtod & ~0xff0000) | (val << 16);
    }
    break;
  case 12:
    ciabsdr = val; break;
  case 13:
    setclr(ciabimask,val); break;
  case 14:
    ciabcra = val;
    if (ciabcra & 0x10){
      ciabcra &= ~0x10;
      ciabta = ciabla;
    }
    break;
    cout << "CIABCRA write" << val << "\n";
  case 15:
    ciabcrb = val; 
    if (ciabcrb & 0x10){
      ciabcrb &= ~0x10;
      ciabtb = ciablb;
    }
    break;
    cout << "CIBACRB write" << val << "\n";
  }
}

void CIA_reset()
{
  ciaatlatch = ciabtlatch = 0;
  ciaatod = ciabtod = 0;
  ciaaicr = ciabicr = ciaaimask = ciabimask = 0;
  ciaacra = ciaacrb = ciabcra = ciabcrb = 0x4; // outmode = toggle;
  div10 = 0; hsync = vsync = 0;
}

void CIA_hsync() { hsync = 1; }
void CIA_vsync() { vsync = 1; }

void CIA_cycle()
{
  int aovfla = 0, aovflb = 0, bovfla = 0, bovflb = 0;
  //int foo = ciaacra | ciaacrb | ciabcra | ciabcrb;

  if (++div10 > 9) {
 //   if (foo & 1) cout << "CIA is ON: ICRs "<< hex << (int)ciaaimask << " " << (int)ciabimask << "!\n";
    div10 = 0;
    // CIA A timers
    if ((ciaacra & 0x21) == 0x01) {
      if (ciaata-- == 0) {
	aovfla = 1;
	if ((ciaacrb & 0x61) == 0x41) {
	  if (ciaatb-- == 0) aovflb = 1;
	}
      } 
    }
    if ((ciaacrb & 0x61) == 0x01) {
      if (ciaatb-- == 0) {
	aovflb = 1;
      }
    }
    // CIA B timers
    if ((ciabcra & 0x21) == 0x01) {
      if (ciabta-- == 0) {
	bovfla = 1;
	if ((ciabcrb & 0x61) == 0x41) {
	  if (ciabtb-- == 0) bovflb = 1;
	}
      } 
    }
    if ((ciabcrb & 0x61) == 0x01) {
      if (ciabtb-- == 0) {
	bovflb = 1;
      }
    }
    if (aovfla) {
      ciaaicr |= 1; RethinkICRA();
      ciaata = ciaala;
      if (ciaacra & 0x8) ciaacra &= ~1;
    }
    if (aovflb) {
      ciaaicr |= 2; RethinkICRA();
      ciaatb = ciaalb;
      if (ciaacrb & 0x8) ciaacrb &= ~1;
    }
    if (bovfla) {
      ciabicr |= 1; RethinkICRB();
      ciabta = ciabla;
      if (ciabcra & 0x8) ciabcra &= ~1;
    }
    if (bovflb) {
      ciabicr |= 2; RethinkICRA();
      ciabtb = ciablb;
      if (ciabcrb & 0x8) ciabcrb &= ~1;
    }
  }
  if (hsync){
    hsync = 0;
    ciabtod++;
    ciabtod &= 0xFFFFFF;
    if (ciabtod == ciabalarm) {
      ciabicr |= 4; RethinkICRB();
    }
  }
  if (vsync){
    vsync = 0;
    ciaatod++;
    ciaatod &= 0xFFFFFF;
    if (ciaatod == ciaaalarm) {
      ciaaicr |= 4; RethinkICRA();
    }
  }
  
}

void dumpcia()
{
  cout << hex << setw(2);
  cout << "A: CRA: " << (int)ciaacra << " CRB: " <<(int)ciaacrb << "IMASK: " <<
    (int)ciaaimask << " TOD: " << ciaatod << (ciaatlatch ? " latched" : "") << 
      " TA: " << ciaata << " TB:" << ciaatb << "\n"; 
  cout << "B: CRA: " << (int)ciabcra << " CRB: " <<(int)ciabcrb << "IMASK: " <<
    (int)ciabimask << " TOD: " << ciabtod << (ciabtlatch ? " latched" : "") << 
      " TA:" << ciabta << " TB:" << ciabtb << "\n"; 
}

UWORD cia64k::wget(UWORD addr)
{
  return bget(addr+1);
}

UBYTE cia64k::bget(UWORD addr)
{
 // cout << "CIA read at " << addr << "\n";
  if ((addr & 0xF0FF) == 0xE001)
    return ReadCIAA((addr & 0xF00) >> 8);
  if ((addr & 0xF0FF) == 0xD000)
    return ReadCIAB((addr & 0xF00) >> 8);
  return 0;
}

void cia64k::wput(UWORD addr,UWORD value)
{
  bput(addr+1,value);
}

void cia64k::bput(UWORD addr,UBYTE value)
{
//  cout << "CIA write " << (int)value << " at " << addr << "\n";
  if ((addr & 0xF0FF) == 0xE001)
    WriteCIAA((addr & 0xF00) >> 8,value);
  if ((addr & 0xF0FF) == 0xD000)
    WriteCIAB((addr & 0xF00) >> 8,value);
}
