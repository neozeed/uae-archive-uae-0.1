 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * custom chip support v0.6
  *
  * v0.0: 950228
  *       provide DMACON/INTENA/INTREQ dummys so that KS gets the memory check
  *       right
  * v0.1: 950304
  *       more registers (partially) implemented
  *       copper and beam position should work, so should interrupts
  * v0.2: 950305
  *       Blitter implemented. Doesn't work.
  *       some b/w playfield support
  * v0.3: 950306
  *       many bug fixes
  * v0.4: 950309
  *       lots more bug fixes. A hand holding a disk appears in the X window!
  *       But it sure looks messed up.
  * v0.5: 950311
  *       hopefully fixed all remaining blitter bugs. The hand and the disk 
  *       look PERFECT!
  * v0.6: 950312
  *       interface to disk code
  *
  * (c) 1995 Bernd Schmidt
  */

#include <iostream>
using namespace std;
#include <iomanip>

#include "amiga.h"
#include "memory.h"
#include "custom.h"
#include "cia.h"
#include "disk.h"
#include "xwin.h"

static const int maxhpos = 227,maxvpos = 312;
static const int dskdelay = 80; // FIXME: ???

UWORD custom64k::wget(UWORD addr)
{
  UWORD (*f)() = cchiptable[(addr >> 1) & 0xFF].get;
  if (f != 0) return (*f)();
  else {
    cout << "Custom read at " << hex << addr << "\n";
    return 0;
  }
}

UBYTE custom64k::bget(UWORD addr)
{
  return wget(addr & 0xfffe) >> (addr & 1 ? 0 : 8);
}

void custom64k::wput(UWORD addr,UWORD value)
{
  void (*f)(UWORD) = cchiptable[(addr >> 1) & 0xFF].put;
  if (f != 0) (*f)(value);
  else {
    cout << "Custom write " << hex << value << " at " << addr << "\n";
  }
}

void custom64k::bput(UWORD addr,UBYTE value)
{
  wput(addr & 0xfffe,value << (addr & 1 ? 0 : 8)); // ??
}

static void setclr(UWORD &p, UWORD val)
{
//  cout << "setclr, new val ";
  if (val & 0x8000) {
    p |= val & 0x7FFF;
  } else {
    p &= ~val;
  }
 // cout << hex << p << "\n";
}

 /* 
  * hardware register values that are visible/can be written to by someone
  */

static UWORD dmacon,intena,intreq,adkcon;
static UWORD vpos, hpos, lof;

static ULONG cop1lc,cop2lc,copcon;

static ULONG bpl1pt,bpl2pt,bpl3pt,bpl4pt,bpl5pt,bpl6pt;
static UWORD bpl1dat,bpl2dat,bpl3dat,bpl4dat,bpl5dat,bpl6dat;
static UWORD bplcon0,bplcon1,bplcon2,bplcon3;
static UWORD diwstrt,diwstop,ddfstrt,ddfstop;
static WORD  bpl1mod,bpl2mod;

static UWORD bltadat,bltbdat,bltcdat,bltddat,bltafwm,bltalwm,bltsize;
static WORD  bltamod,bltbmod,bltcmod,bltdmod;
static UWORD bltcon0,bltcon1;
static ULONG bltapt,bltbpt,bltcpt,bltdpt,bltcnxlpt,bltdnxlpt;

static ULONG dskpt;
static UWORD dsklen,dsksync;

 /*
  * "hidden" hardware registers
  */

static ULONG coplc;
static UWORD copi1,copi2;

static enum {
  COP_stop, COP_read, COP_wait, COP_move, COP_skip  
} copstate;

static UWORD vblitsize,hblitsize,blitpreva,blitprevb;
static UWORD blitlpos,blitashift,blitbshift,blinea,blineb;
static bool blitline,blitfc,blitzero,blitfill,blitife,blitdesc,blitsing;
static bool blitonedot,blitsign;
static long int bltwait;

static enum {
  BLT_done, BLT_init, BLT_read, BLT_work, BLT_write, BLT_next,BLT_wait
} bltstate;

static UWORD plffirstline,plflastline,plffirstword,plflastword;

static enum {
  PF_above, PF_left, PF_within, PF_right, PF_below
} pfieldstate;

static UWORD dskmfm,dskbyte,dskdmaen,dsktime;
static bool dsksynced;

 /*
  * helper functions
  */

static void calcdiw()
{
  plffirstline = diwstrt >> 8;
  plflastline = diwstop >> 8;
  if ((plflastline & 0x80) == 0) plflastline |= 0x100;
  plffirstword = (ddfstrt & 0x1FC) * 4;
  plflastword = ((ddfstop & 0x1FC)+8) *4;
}

 /* 
  * register functions
  */

static UWORD DMACONR() 
{
  return dmacon | (bltstate==BLT_done ? 0 : 0x4000)
    | (blitzero ? 0x2000 : 0); 
}
static UWORD INTENAR() { return intena; }
static UWORD INTREQR() { return intreq; }
static UWORD ADKCONR() { return adkcon; }
static UWORD VPOSR() { return (vpos >> 8) | lof; }
static UWORD VHPOSR() { return (vpos << 8) | hpos; } 

static void  DMACON(UWORD v) { setclr(dmacon,v); dmacon &= 0x1FFF; }
static void  INTENA(UWORD v) { setclr(intena,v); }
static void  INTREQ(UWORD v) { setclr(intreq,v); }
static void  ADKCON(UWORD v) { setclr(adkcon,v); }

static void  BPL1PTH(UWORD v) { bpl1pt= (bpl1pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL1PTL(UWORD v) { bpl1pt= (bpl1pt & ~0xffff) | v; }
static void  BPL2PTH(UWORD v) { bpl2pt= (bpl2pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL2PTL(UWORD v) { bpl2pt= (bpl2pt & ~0xffff) | v; }
static void  BPL3PTH(UWORD v) { bpl3pt= (bpl3pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL3PTL(UWORD v) { bpl3pt= (bpl3pt & ~0xffff) | v; }
static void  BPL4PTH(UWORD v) { bpl4pt= (bpl4pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL4PTL(UWORD v) { bpl4pt= (bpl4pt & ~0xffff) | v; }
static void  BPL5PTH(UWORD v) { bpl5pt= (bpl5pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL5PTL(UWORD v) { bpl5pt= (bpl5pt & ~0xffff) | v; }
static void  BPL6PTH(UWORD v) { bpl6pt= (bpl6pt & 0xffff) | ((ULONG)v << 16); }
static void  BPL6PTL(UWORD v) { bpl6pt= (bpl6pt & ~0xffff) | v; }

static void  BPLCON0(UWORD v) { bplcon0 = v; }
static void  BPLCON1(UWORD v) { bplcon1 = v; }
static void  BPLCON2(UWORD v) { bplcon2 = v; }
static void  BPLCON3(UWORD v) { bplcon3 = v; }

static void  BPL1MOD(UWORD v) { bpl1mod = v; }
static void  BPL2MOD(UWORD v) { bpl2mod = v; }

static void  BPL1DAT(UWORD v) { bpl1dat = v; }
static void  BPL2DAT(UWORD v) { bpl2dat = v; }
static void  BPL3DAT(UWORD v) { bpl3dat = v; }
static void  BPL4DAT(UWORD v) { bpl4dat = v; }
static void  BPL5DAT(UWORD v) { bpl5dat = v; }
static void  BPL6DAT(UWORD v) { bpl6dat = v; }

static void  DIWSTRT(UWORD v) { diwstrt = v; calcdiw(); }
static void  DIWSTOP(UWORD v) { diwstop = v; calcdiw(); }
static void  DDFSTRT(UWORD v) { ddfstrt = v; calcdiw(); }
static void  DDFSTOP(UWORD v) { ddfstop = v; calcdiw(); }

static void  COP1LCH(UWORD v) { cop1lc= (cop1lc & 0xffff) | ((ULONG)v << 16); }
static void  COP1LCL(UWORD v) { cop1lc= (cop1lc & ~0xffff) | v; }
static void  COP2LCH(UWORD v) { cop2lc= (cop2lc & 0xffff) | ((ULONG)v << 16); }
static void  COP2LCL(UWORD v) { cop2lc= (cop2lc & ~0xffff) | v; }

static UWORD COPJMPR1() { coplc = cop1lc; copstate = COP_read; cout << "CJMP1\n"; return 0; }
static UWORD COPJMPR2() { coplc = cop2lc; copstate = COP_read; cout << "CJMP2\n"; return 0; }
static void  COPJMP1(UWORD) { coplc = cop1lc; copstate = COP_read; cout << "CJMP1\n"; }
static void  COPJMP2(UWORD) { coplc = cop2lc; copstate = COP_read; cout << "CJMP2\n"; }

static void  BLTADAT(UWORD v) { bltadat = v; }
static void  BLTBDAT(UWORD v) { bltbdat = v; }
static void  BLTCDAT(UWORD v) { bltcdat = v; }

static void  BLTAMOD(UWORD v) { bltamod = v; }
static void  BLTBMOD(UWORD v) { bltbmod = v; }
static void  BLTCMOD(UWORD v) { bltcmod = v; }
static void  BLTDMOD(UWORD v) { bltdmod = v; }

static void  BLTCON0(UWORD v) { bltcon0 = v; }
static void  BLTCON1(UWORD v) { bltcon1 = v; }

static void  BLTAFWM(UWORD v) { bltafwm = v; }
static void  BLTALWM(UWORD v) { bltalwm = v; }

static void  BLTAPTH(UWORD v) { bltapt= (bltapt & 0xffff) | ((ULONG)v << 16); }
static void  BLTAPTL(UWORD v) { bltapt= (bltapt & ~0xffff) | (v); }
static void  BLTBPTH(UWORD v) { bltbpt= (bltbpt & 0xffff) | ((ULONG)v << 16); }
static void  BLTBPTL(UWORD v) { bltbpt= (bltbpt & ~0xffff) | (v); }
static void  BLTCPTH(UWORD v) { bltcpt= (bltcpt & 0xffff) | ((ULONG)v << 16); }
static void  BLTCPTL(UWORD v) { bltcpt= (bltcpt & ~0xffff) | (v); }
static void  BLTDPTH(UWORD v) { bltdpt= (bltdpt & 0xffff) | ((ULONG)v << 16); }
static void  BLTDPTL(UWORD v) { bltdpt= (bltdpt & ~0xffff) | (v); }
static void  BLTSIZE(UWORD v) { bltsize = v; bltstate = BLT_init; }

static void  DSKSYNC(UWORD v) { dsksync = v; }
static void  DSKDAT(UWORD v) { dskmfm = v; }
static void  DSKPTH(UWORD v) { dskpt = (dskpt & 0xffff) | ((ULONG)v << 16); }
static void  DSKPTL(UWORD v) { dskpt = (dskpt & ~0xffff) | (v); }

static void  DSKLEN(UWORD v) 
{
  if (v & 0x8000) { dskdmaen++; } else { dskdmaen = 0; }
  dsktime = dskdelay; dsksynced = false;
  dsklen = v;
}

static UWORD DSKBYTR() 
{
  UWORD v = dsklen >> 1 & 0x6000;
  v |= dskbyte;
  dskbyte &= ~0x8000;
  if (dsksync == dskmfm) v |= 0x1000;
  return v;
}

static UWORD DSKDATR() { return dskmfm; }

custom64k::custom_func custom64k::functable[] = {
  { 0x002,  { &DMACONR, 0 }},
  { 0x096,  { 0, DMACON  }},
  { 0x01C,  { &INTENAR, 0 }},
  { 0x09A,  { 0, INTENA  }},
  { 0x01E,  { &INTREQR, 0 }},
  { 0x09C,  { 0, INTREQ  }},
  { 0x010,  { &ADKCONR, 0 }},
  { 0x09E,  { 0, ADKCON  }},

  { 0x006,  { &VHPOSR, 0 }},
  { 0x004,  { &VPOSR, 0 }},

  { 0x080,  { 0, &COP1LCH }},
  { 0x082,  { 0, &COP1LCL }},
  { 0x084,  { 0, &COP2LCH }},
  { 0x086,  { 0, &COP2LCL }},

  { 0x088,  { &COPJMPR1, &COPJMP1 }},
  { 0x08A,  { &COPJMPR2, &COPJMP2 }},

  { 0x08E,  { 0, &DIWSTRT }},
  { 0x090,  { 0, &DIWSTOP }},
  { 0x092,  { 0, &DDFSTRT }},
  { 0x094,  { 0, &DDFSTOP }},

  { 0x0E0,  { 0, &BPL1PTH }},
  { 0x0E2,  { 0, &BPL1PTL }},
  { 0x0E4,  { 0, &BPL2PTH }},
  { 0x0E6,  { 0, &BPL2PTL }},
  { 0x0E8,  { 0, &BPL3PTH }},
  { 0x0EA,  { 0, &BPL3PTL }},
  { 0x0EC,  { 0, &BPL4PTH }},
  { 0x0EE,  { 0, &BPL4PTL }},
  { 0x0F0,  { 0, &BPL5PTH }},
  { 0x0F2,  { 0, &BPL5PTL }},
  { 0x0F4,  { 0, &BPL6PTH }},
  { 0x0F6,  { 0, &BPL6PTL }},

  { 0x100,  { 0, &BPLCON0 }},
  { 0x102,  { 0, &BPLCON1 }},
  { 0x104,  { 0, &BPLCON2 }},
  { 0x106,  { 0, &BPLCON3 }},

  { 0x108,  { 0, &BPL1MOD }},
  { 0x10A,  { 0, &BPL2MOD }},

  { 0x110,  { 0, &BPL1DAT }},
  { 0x112,  { 0, &BPL2DAT }},
  { 0x114,  { 0, &BPL3DAT }},
  { 0x116,  { 0, &BPL4DAT }},
  { 0x118,  { 0, &BPL5DAT }},
  { 0x11A,  { 0, &BPL6DAT }},

  { 0x040,  { 0, &BLTCON0 }},
  { 0x042,  { 0, &BLTCON1 }},

  { 0x044,  { 0, &BLTAFWM }},
  { 0x046,  { 0, &BLTALWM }},

  { 0x064,  { 0, &BLTAMOD }},
  { 0x062,  { 0, &BLTBMOD }},
  { 0x060,  { 0, &BLTCMOD }},
  { 0x066,  { 0, &BLTDMOD }},

  { 0x050,  { 0, &BLTAPTH }},
  { 0x052,  { 0, &BLTAPTL }},
  { 0x04C,  { 0, &BLTBPTH }},
  { 0x04E,  { 0, &BLTBPTL }},
  { 0x048,  { 0, &BLTCPTH }},
  { 0x04A,  { 0, &BLTCPTL }},
  { 0x054,  { 0, &BLTDPTH }},
  { 0x056,  { 0, &BLTDPTL }},

  { 0x070,  { 0, &BLTCDAT }},
  { 0x072,  { 0, &BLTBDAT }},
  { 0x074,  { 0, &BLTADAT }},

  { 0x058,  { 0, &BLTSIZE }},

  { 0x008,  { &DSKDATR, 0 }},
  { 0x01A,  { &DSKBYTR, 0 }},
  { 0x026,  { 0, &DSKDAT }},
  { 0x024,  { 0, &DSKLEN }},
  { 0x020,  { 0, &DSKPTH }},
  { 0x022,  { 0, &DSKPTL }},
  { 0x07E,  { 0, &DSKSYNC }},

  { 0xFFFF, { 0, 0 }}
};

custom64k::cchipaddr *custom64k::cchiptable = 0;

custom64k::custom64k()
{
  if (!cchiptable){
    // CUSTOM CHIP INITIALIZATION
    // build function pointer table
    cchiptable = new cchipaddr[256];
    for (int i=0;i<256;i++) {
      cchiptable[i].get = 0;
      cchiptable[i].put = 0;
    }
    for(custom_func *f = functable; f->addr != 0xffff; f++){
      cchiptable[f->addr >> 1] = f->fptrs;
    }
    customreset();
  }
}

void customreset()
{
  // reset some values
  vpos = hpos = lof = 0;
  dmacon = intena = 0;
  bltstate = BLT_done;
  copstate = COP_stop;
  copcon = 0;
  dskdmaen = 0;
}

static bool dmaen(UWORD dmamask)
{
  return (dmamask & dmacon) && (dmacon & 0x200);
}

static bool copcomp()
{
  UWORD vp = vpos & ((copi2 >> 8) & 0x7F);
  UWORD hp = hpos & (copi2 & 0xFE);
  UWORD vcmp = copi1 >> 8;
  UWORD hcmp = copi1 & 0xFE;
  return (vp >= vcmp) && (hp >= hcmp) && ((copi2 & 0x8000) /*|| bfd FIXME*/);  
}

static void do_copper()
{
  switch(copstate){
  case COP_read:
    if (dmaen(0x80)){
 //     cout << "copper read at " << hex << coplc << "\n";
      copi1 = mem.get_word(coplc); 
      copi2 = mem.get_word(coplc+2);
      coplc += 4;
      copstate = (copi1 & 1) ? (copi2 & 1) ? COP_skip : COP_wait : COP_move;
    } 
    break;
  case COP_move:
 //   cout << "copper move " << hex << copi1 << " " << copi2 << "\n";
    if (copi1 >= (copcon & 2 ? 0x40 : 0x80))
      mem.put_word(0xDFF000+copi1,copi2);
    copstate = COP_read; // FIXME: copdang
    break;
  case COP_skip:
    if (copcomp()) coplc += 4;
    copstate = COP_read;
    break;
  case COP_wait:
    if (copcomp()) copstate = COP_read;
    break;
  case COP_stop:
    break;
  }
}

static void blitter_read()
{
  if (bltcon0 & 0xe00){
    if (!dmaen(0x40)) return; // blitter stopped
    if (!blitline){
      if (bltcon0 & 0x800) bltadat = mem.get_word(bltapt);
      if (bltcon0 & 0x400) bltbdat = mem.get_word(bltbpt);
    }
    if (bltcon0 & 0x200) bltcdat = mem.get_word(bltcpt);
  }
  bltstate = BLT_work;
}

static void blitter_write()
{
  if (bltddat) blitzero = false;
  if (bltcon0 & 0x100){
    if (!dmaen(0x40)) return;
    mem.put_word(bltdpt,bltddat);
  }
  bltstate = BLT_next;
}

static void blitter_blit()
{
  UWORD blitahold,blitbhold,blitchold;

  if (blitdesc) { // aaarggh!
    UWORD bltamask = 0xffff;

    if (!blitlpos) { bltamask &= bltafwm; }
    if (blitlpos == (hblitsize - 1)) { bltamask &= bltalwm; }

    // FIXME? The following two lines are probably not 100% exact
    blitahold = ((ULONG)((bltadat & bltamask) << 16) | blitpreva) 
      >> (16-blitashift);
    blitbhold = ((ULONG)(bltbdat << 16) | blitprevb) >> (16-blitbshift);
    blitchold = bltcdat;
  } else {
    UWORD bltamask = 0xffff;

    if (!blitlpos) { bltamask &= bltafwm; }
    if (blitlpos == (hblitsize - 1)) { bltamask &= bltalwm; }
    // FIXME? The following two lines are probably not 100% exact
    blitahold = ((ULONG)(blitpreva << 16) | (bltadat&bltamask)) >> blitashift;
    blitbhold = ((ULONG)(blitprevb << 16) | bltbdat) >> blitbshift;
    blitchold = bltcdat;
  }
  bltddat = 0;
  for(int i=0; i<16; i++) {
    // oh dear, this is SLOW!
    int minterm = ((blitahold >> 13) & 4) | ((blitbhold >> 14) & 2)
      | ((blitchold >> 15) & 1);
    blitahold <<= 1; blitbhold <<= 1; blitchold <<= 1;
    if (bltcon0 & (1 << minterm)) 
      bltddat |= (1 << (15-i));
  }
  if (blitfill){
    UWORD fillmask = 1;
    for (;fillmask;fillmask <<= 1){
      UWORD tmp = bltddat;
      if (blitfc) {
	if (blitife) bltddat |= fillmask; else bltddat ^= fillmask;
      }
      if (tmp & fillmask) blitfc = !blitfc;
    } 
  }
  bltstate = BLT_write;
  blitpreva = bltadat; blitprevb = bltbdat;
}

static void blitter_nxblit()
{
  bltstate = BLT_read;
  if (blitdesc){
    if (++blitlpos == hblitsize) {
      if (--vblitsize == 0) {
	bltstate = BLT_done; mem.put_word(0xDFF09C,0x8040);
      }
      blitlpos = 0;
      if (bltcon0 & 0x800) bltapt -= 2+bltamod; 
      if (bltcon0 & 0x400) bltbpt -= 2+bltbmod; 
      if (bltcon0 & 0x200) bltcpt -= 2+bltcmod; 
      if (bltcon0 & 0x100) bltdpt -= 2+bltdmod;
    } else {
      if (bltcon0 & 0x800) bltapt -= 2; 
      if (bltcon0 & 0x400) bltbpt -= 2; 
      if (bltcon0 & 0x200) bltcpt -= 2; 
      if (bltcon0 & 0x100) bltdpt -= 2;

    }
  } else {
    if (++blitlpos == hblitsize) {
      if (--vblitsize == 0) { 
	bltstate = BLT_done; mem.put_word(0xDFF09C,0x8040);
      }
      blitlpos = 0;
      if (bltcon0 & 0x800) bltapt += 2+bltamod; 
      if (bltcon0 & 0x400) bltbpt += 2+bltbmod; 
      if (bltcon0 & 0x200) bltcpt += 2+bltcmod; 
      if (bltcon0 & 0x100) bltdpt += 2+bltdmod;
    } else {
      if (bltcon0 & 0x800) bltapt += 2; 
      if (bltcon0 & 0x400) bltbpt += 2; 
      if (bltcon0 & 0x200) bltcpt += 2; 
      if (bltcon0 & 0x100) bltdpt += 2;
    }
  }
}

static void blitter_line_incx(int =1)
{
  blinea >>= 1;
  if (!blinea) {
    blinea = 0x8000;
    bltcnxlpt += 2;
    bltdnxlpt += 2;
  }
}

static void blitter_line_decx(int shm=1)
{
  if (shm) blineb = (blineb << 2) | (blineb >> 14);
  blinea <<= 1;
  if (!blinea) {
    blinea = 1;
    bltcnxlpt -= 2;
    bltdnxlpt -= 2;
  }
}

static void blitter_line_decy(int shm=1)
{
  if (shm) blineb = (blineb >> 1) | (blineb << 15);
  bltcnxlpt -= bltcmod;
  bltdnxlpt -= bltcmod; // ??? ??? ??? am I wrong or doesn't KS1.3 set bltdmod?
  blitonedot = 0;
}

static void blitter_line_incy(int shm=1)
{
  if (shm) blineb = (blineb >> 1) | (blineb << 15);
  bltcnxlpt += bltcmod;
  bltdnxlpt += bltcmod; // ??? ??? ???
  blitonedot = 0;
}

static void blitter_line()
{
  bltddat = 0;
  UWORD blitahold = blinea, blitbhold = blineb, blitchold = bltcdat;
  if (blitsing && blitonedot) blitahold = 0;
  blitonedot = 1;
  for(int i=0; i<16; i++) {
    int minterm = ((blitahold >> 13) & 4) | ((blitbhold >> 14) & 2)
      | ((blitchold >> 15) & 1);
    blitahold <<= 1; blitbhold <<= 1; blitchold <<= 1;
    if (bltcon0 & (1 << minterm)) bltddat |= (1 << (15-i));
  }
  if (!blitsign){
    bltapt += (WORD)bltamod;
    if (bltcon1 & 0x10){
      if (bltcon1 & 0x8) blitter_line_decy(0); else blitter_line_incy(0);
    } else {
      if (bltcon1 & 0x8) blitter_line_decx(0); else blitter_line_incx(0);
    }
  } else {
    bltapt += (WORD)bltbmod;
  }
  if (bltcon1 & 0x10){
    if (bltcon1 & 0x4) blitter_line_decx(); else blitter_line_incx();
  } else {
    if (bltcon1 & 0x4) blitter_line_decy(); else blitter_line_incy();
  }
  blitsign = 0 > (WORD)bltapt;
  bltstate = BLT_write;
}

static void blitter_nxline()
{
  if (--vblitsize == 0) {
    bltstate = BLT_wait; bltwait = /*0xE2*31*/2; mem.put_word(0xDFF09C,0x8040);
  } else {
    bltstate = BLT_read;
    bltcpt = bltcnxlpt;
    bltdpt = bltdnxlpt;
  }
}

static void do_blitter()
{
  switch(bltstate) {
  case BLT_init:
    cout << "Blit started!";
    vblitsize = bltsize >> 6;
    hblitsize = bltsize & 0x3F;
    if (!vblitsize) vblitsize = 1024;
    if (!hblitsize) hblitsize = 64;
    blitlpos = 0;
    blitzero = true; blitpreva = blitprevb = 0;
    blitline = bltcon1 & 1;
    blitashift = bltcon0 >> 12; blitbshift = bltcon1 >> 12;
    
    if (blitline) {
      cout << " line\n";
      bltcnxlpt = bltcpt;
      bltdnxlpt = bltdpt;
      blitsing = bltcon1 & 0x2;
      blinea = bltadat >> blitashift;
      blineb = (bltbdat >> blitbshift) | (bltbdat << (16-blitbshift));
      blitsign = bltcon1 & 0x40; 
      blitonedot = false;
      bltamod &= 0xfffe; bltbmod &= 0xfffe; bltapt &= 0xfffe;
    } else {
      blitfc = bltcon1 & 0x4;
      blitife = bltcon1 & 0x8;
      blitfill = bltcon1 & 0x18;
      if (blitfill) cout << " fill";
      if ((bltcon1 & 0x18) == 0x18) cout << "STRANGE FILL MODE ALERT!!\n";
      blitdesc = bltcon1 & 0x2;
      if(blitdesc) cout << " desc";
      cout << "\n";
    }
    bltstate = BLT_read;
    break;
  case BLT_read:
    blitter_read();
    break;
  case BLT_write:
    blitter_write();
    break;
  case BLT_work:
    if (blitline) blitter_line(); else blitter_blit();
    break;
  case BLT_next:
    if (blitline) blitter_nxline(); else blitter_nxblit();
    break;
  case BLT_wait:
    if (!--bltwait) bltstate = BLT_done;
    break;
  case BLT_done:
    break;
  }
}

  // note: fall-throughs in all playfield functions

static void pfield_fetchdata()
{
  if (/*dmaen(0x100)*/ 1) {
    switch(bplcon0 & 0x7000){
    case 0x6000:
      bpl6dat = mem.get_word(bpl6pt); bpl6pt += 2;
    case 0x5000:
      bpl5dat = mem.get_word(bpl5pt); bpl5pt += 2;
    case 0x4000:
      bpl4dat = mem.get_word(bpl4pt); bpl4pt += 2;
    case 0x3000:
      bpl3dat = mem.get_word(bpl3pt); bpl3pt += 2;
    case 0x2000:
      bpl2dat = mem.get_word(bpl2pt); bpl2pt += 2;
    case 0x1000:
      bpl1dat = mem.get_word(bpl1pt); bpl1pt += 2;
    }
  }
}

static void pfield_hsync()
{
  if (/*dmaen(0x100)*/ 1) {
    switch(bplcon0 & 0x7000){
    case 0x6000:
      bpl6pt += bpl2mod;
    case 0x5000:
      bpl5pt += bpl1mod;
    case 0x4000:
      bpl4pt += bpl2mod;
    case 0x3000:
      bpl3pt += bpl1mod;
    case 0x2000:
      bpl2pt += bpl2mod;
    case 0x1000:
      bpl1pt += bpl1mod;
    }
  }
}

static UWORD pfield_nextpixel()
{
  UWORD tmp = 0;
  switch(bplcon0 & 0x7000){
  case 0x6000: 
    tmp = (bpl6dat >> 15); bpl6dat <<= 1;
  case 0x5000: 
    tmp = (tmp << 1) | (bpl5dat >> 15); bpl5dat <<= 1;
  case 0x4000: 
    tmp = (tmp << 1) | (bpl4dat >> 15); bpl4dat <<= 1;
  case 0x3000: 
    tmp = (tmp << 1) | (bpl3dat >> 15); bpl3dat <<= 1;
  case 0x2000: 
    tmp = (tmp << 1) | (bpl2dat >> 15); bpl2dat <<= 1;
  case 0x1000: 
    tmp = (tmp << 1) | (bpl1dat >> 15); bpl1dat <<= 1;
  } 
  return tmp;
}

static void do_playfield()
{
  int xpos = hpos * 4;
  if (vpos >= plffirstline && vpos < plflastline){
    if (xpos >= plffirstword && xpos < plflastword){
      if (bplcon0 & 0x8000){
	if ((xpos & 0xF) == 0){
	  pfield_fetchdata();
	  for(int i=0;i<16;i++){
	    UWORD pixel = pfield_nextpixel() & 3;
	    int col;
	    if (pixel == 3) col = 1;
	    else if (pixel == 0) col = 0;
	    else col = (pixel & 1) == ((xpos ^ vpos) & 1);
	    DrawPixel(xpos+i,vpos,col);
	  }
	}
      } else {
	if ((xpos & 0x1F) == 0){
	  pfield_fetchdata();
	  for(int i=0;i<16;i++){
	    UWORD pixel = pfield_nextpixel() & 3;
	    int col;
	    if (pixel == 3) col = 1;
	    else if (pixel == 0) col = 0;
	    else col = (pixel & 1) == ((xpos ^ vpos) & 1);
	    DrawPixel(xpos+i*2,vpos,col);
	    DrawPixel(xpos+i*2+1,vpos,col);
	  }
	}
      }
    }
  }
}

static void do_disk()
{
  if (dskdmaen > 1 && dmaen(0x10)){
    if (--dsktime == 0) {
      dsktime = dskdelay;
      if (dsklen & 0x4000){
	// write
      } else {
	DISK_GetData(dskmfm,dskbyte);
	dskbyte |= 0x8000;
	if (dskmfm == dsksync) {
	  mem.put_word(0xDFF09C,0x9000);
	  dsksynced = true;
	}
	if (dsksynced || !(adkcon & 0x400)){
	  mem.put_word(dskpt,dskmfm); dskpt+=2;
	}
      }
    }
  }
}

static void do_vsync()
{
//  intreq |= 1 << i_vertb;
  mem.put_word(0xDFF09C,0x8020);
  lof ^= 1;
//  if (copstate != COP_stop)
    mem.put_word(0xDFF088,0); // restart copper
  CIA_vsync();
}

void clockcycle()
{
  if (++hpos == maxhpos){
    hpos = 0; CIA_hsync();
    pfield_hsync();
    if (++vpos == maxvpos) {
      vpos = 0;
      do_vsync();
    }
  }
  do_copper();
  do_playfield();
  do_blitter();
  do_disk();
  CIA_cycle();
}

void dumpcustom()
{
  cout << "DMACON: " << DMACONR() << " INTENA: " << intena << " INTREQ: "
    << intreq << " VPOS: " << vpos << " HPOS: " << hpos << "\n";
}

int intlev()
{
  UWORD imask = intreq & intena;
  if (imask && (intena & 0x4000)){
 //   cout << hex << "intreq " << intreq << " intena " << intena <<"\n";
    if (imask & 0x2000) return 6;
    if (imask & 0x1800) return 5;
    if (imask & 0x0780) return 4;
    if (imask & 0x0070) return 3;
    if (imask & 0x0008) return 2;
    if (imask & 0x0007) return 1;
  }
  return -1;
}
