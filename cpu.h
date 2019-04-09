 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * CPU emulation v0.0
  * 0.0: 
  * 
  * (c) 1994 Bernd Schmidt
  * 
  */

extern void MC68000_init();
extern void MC68000_step();
extern void MC68000_skip(CPTR);
extern void MC68000_dumpstate(CPTR &);
extern void MC68000_disasm(CPTR,CPTR &,int=1);
extern void MC68000_reset();
