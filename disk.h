 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * disk support v0.0
  * v0.0: 950312
  *
  * (c) 1995 Bernd Schmidt
  */

void DISK_init();
void DISK_select(UBYTE data);
UBYTE DISK_status();
void DISK_GetData(UWORD &mfm,UWORD &byt);
