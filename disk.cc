 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * disk support v0.0
  * v0.0: 950312
  *
  * (c) 1995 Bernd Schmidt
  */

#include <iostream.h>
#include <iomanip.h>
#include <unistd.h>
#include <stdio.h>

#include "amiga.h"
#include "disk.h"

static int side, direction, step;
static UBYTE selected = 15;
static bool dskready;

class drive {
  FILE *diskfile;
  unsigned int track;
  unsigned int motoroff;
  unsigned int trackoffs,insecpos;
  bool secok;
  UBYTE secbuf[544];
  UWORD mfmbuf[544];
  ULONG getseculong(unsigned int offset);
  ULONG getmfmulong(unsigned int offset);
  void readsec();
public:
  drive(char *fname=0);
  void eject();
  void insert(char *fname);
  void step();
  bool track0() { return track == 0; }
  bool empty() { return diskfile == 0; }
  void motor(int off);
  void GetData(UWORD &,UWORD &);
};

drive::drive(char *fname)
{
  diskfile = 0;
  if (fname != 0) insert(fname);
  track = 0;
  motoroff = 1;
}

void drive::eject()
{
  if (diskfile != 0) fclose(diskfile);
  diskfile = 0;
}

void drive::insert(char *fname)
{
  diskfile = fopen(fname,"r+");
}

void drive::step()
{
  if (direction) {
    if (track) track--;
  } else {
    if(track<85) track++;
  }
}

void drive::motor(int off) 
{
  if (motoroff && !off) { trackoffs = 0; insecpos = 0; secok = false; }
  motoroff = off;
}

ULONG drive::getmfmulong(unsigned int offs)
{
  return (mfmbuf[offs] << 16) | mfmbuf[offs+1];
}

ULONG drive::getseculong(unsigned int offs)
{
  return (secbuf[offs] << 24) | (secbuf[offs+1] << 16) | (secbuf[offs+2] << 8)
    | (secbuf[offs+3]);
}

void drive::readsec()
{
  int i;
  secbuf[0] = secbuf[1] = 0x00;
  secbuf[2] = secbuf[3] = 0xa1;
  secbuf[4] = 0xff;
  secbuf[5] = track*2+side;
  secbuf[6] = trackoffs;
  secbuf[7] = 10-trackoffs;
  for(i=8;i<24;i++) secbuf[i] = 0;
  fseek(diskfile,((track*2+side)*11+trackoffs)*512,SEEK_SET);
  fread(&secbuf[32],sizeof(UBYTE),512,diskfile);

  mfmbuf[0] = mfmbuf[1] = 0xaaaa;
  mfmbuf[2] = mfmbuf[3] = 0x4489;
  ULONG deven,dodd;
  deven = getseculong(4); dodd = deven >> 1;
  deven &= 0x55555555; dodd &= 0x55555555;
  mfmbuf[4] = dodd >> 16; mfmbuf[5] = dodd;
  mfmbuf[6] = deven>> 16; mfmbuf[7] = deven;
  for (i=8;i<48;i++) mfmbuf[i] = 0;
  for (i=0;i<512;i+=4){
    deven = getseculong(i+32);
    dodd = deven >> 1;
    deven &= 0x55555555; dodd &= 0x55555555;
    mfmbuf[(i>>1)+32] = dodd >> 16; mfmbuf[(i>>1)+33] = dodd;
    mfmbuf[(i>>1)+256+32] = deven>> 16; mfmbuf[(i>>1)+256+33] = deven;
  }
  
  ULONG hck=0,dck=0;
  for(i=4;i<24;i+=2) hck += getmfmulong(i);
  deven = dodd = hck; dodd >>= 1;
  mfmbuf[24] = dodd >> 16; mfmbuf[25] = dodd;
  mfmbuf[26] = deven>> 16; mfmbuf[27] = deven;
  for(i=32;i<544;i+=2) dck += getmfmulong(i);
  deven = dodd = dck; dodd >>= 1;
  mfmbuf[28] = dodd >> 16; mfmbuf[29] = dodd;
  mfmbuf[30] = deven>> 16; mfmbuf[31] = deven;  
  secok = true;
}

void drive::GetData(UWORD &mfm,UWORD &byt)
{
  if (!secok) readsec();
  mfm = mfmbuf[insecpos]; byt = secbuf[insecpos++];
  if (insecpos == 544){
    secok = false; 
    if (++trackoffs == 11) trackoffs = 0;
  }
}

static drive floppy[4];

void DISK_init()
{
  floppy[0].insert("wb13.adf");
}

void DISK_select(UBYTE data)
{
  int step_pulse;
  if (selected != ((data >> 3) & 15)) dskready = false;
  selected = (data >> 3) & 15;
  side = (data >> 2) & 1;
  direction = (data >> 1) & 1;
  step_pulse = data & 1;
  if (step != step_pulse) {
    step = step_pulse;
    if (step == 0){
      for (int dr=0;dr<4;dr++){
	if (!(selected & (1<<dr))) {
	  floppy[dr].step();
	}
      }
    }
  }
  for (int dr=0;dr<4;dr++){
    if (!(selected & (1<<dr))) {
      floppy[dr].motor(data >> 7);
    }
  }
}

UBYTE DISK_status()
{
  UBYTE st = 0x3c;
  if (dskready) st &= ~0x20;
  dskready = true;
  for (int dr=0;dr<4;dr++){
    if (!(selected & (1 << dr))) {
      if (floppy[dr].track0()) { st &= ~0x10; }
      if (floppy[dr].empty()) { st &= ~0x4; }
    }
  }
  return st;
}

void DISK_GetData(UWORD &mfm,UWORD &byt)
{
  for (int dr=0;dr<4;dr++){
    if (!(selected & (1<<dr))) {
      floppy[dr].GetData(mfm,byt);
    }
  }
}
