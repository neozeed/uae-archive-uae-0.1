 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * MC68000 emulation v0.5
  *
  * v0.0: 950227
  *       instruction decoding, emulate all instructions except MOVEM and
  *       things like MOVE SR,EA or RESET
  *       debugging functions: disasm, dumpstate
  * v0.1: 950228 
  *       exceptions, almost all instructions, bug fixes
  * v0.2: 950305 
  *       more bug fixes in interrupt code. STOP implemented. Bugs in
  *       GenerateDecTab() fixed.
  * v0.3: 950306
  *       bug fixes: ROR shouldn't set X flag. SWAP didn't set any flags.
  *       GenerateDecTab() created wrong sizes for all shift instructions
  * v0.4: 950309
  *       yet another bug in the shifting instructions part of GenerateDecTab
  * v0.5: 930311
  *       lots of bug fixes. Sign extension is probably correct now.
  *       Multiplication corrected. None of these bugs affected the Kickstart.
  *
  * (c) 1995 Bernd Schmidt
  * 
  */

#include <iostream.h>
#include <iomanip.h>
#include <assert.h>
#include <string.h>

#include "amiga.h"
#include "memory.h"
#include "custom.h"
#include "cpu.h"

enum amodes {
  Dreg, Areg, Aind, Aipi, Apdi, Ad16, Ad8r, 
  absw, absl, PC16, PC8r, imm, imm3, ill2, ill3 
};

class addr_mode {
 public:
  amodes mode;
  UBYTE reg;
  addr_mode(int=0,int=0);
};

addr_mode::addr_mode(int m,int r)
{
  assert ((r & 7) == r);
  assert ((m & 7) == m);

  if (m == 7) mode = (amodes)(absw + r);
  else mode = (amodes)m;
  reg = r;
}

enum eatypes {
  RegD, RegA, Addr, EAIM
};

struct effadr {
 public:
  ULONG addr;
  eatypes type;
  ULONG szmask;
};

struct instr_params {
  addr_mode src,dest;
  ULONG mask;
  UWORD clogs;
};

typedef void instr_result;
typedef instr_result (*instr_func)(const instr_params &);

struct I_dec_tab_entry {
  instr_func execfunc;
  instr_params params;
  I_dec_tab_entry() { execfunc = NULL; }
}; 

static I_dec_tab_entry instr_dectab[65536]; 

static void GenerateDecTab();

void MC68000_init()
{
    GenerateDecTab();
};

typedef int flagtype; // faster than char ?

static struct {
  ULONG d[8];
  CPTR  a[8],usp;
  UWORD sr;
  flagtype t,s,x,n,z,v,c,stopped;
  int intmask;
  ULONG pc;
} regs;

static ULONG sizemask(const int sz)
{
  switch(sz){
  case 0: return 0xff;
  case 1: return 0xffff;
  case 2: return 0xffffffff;
  default: abort();
  }
}

static inline int mask2len(const ULONG mask)
{
  if (mask == 0xff) return 1;
  if (mask == 0xffff) return 2;
  return 4;
}

static inline int mask2shift(const ULONG mask)
{
  if (mask == 0xff) return 7;
  if (mask == 0xffff) return 15;
  return 31;
}

static inline ULONG ExtendWord(const ULONG v)
{
  return (LONG)(WORD)(v);
}

static inline ULONG ExtendByte(const ULONG v)
{
  return (LONG)(BYTE)(v);
}

static inline ULONG Extend(const ULONG v,const ULONG mask)
{
  switch(mask){
  case 0xff: 
    return (LONG)(BYTE)v;
  case 0xffff:
    return (LONG)(WORD)v;
  default:
    return v;
  }
}

static inline UWORD nextiword()
{
  UWORD r = mem.get_word(regs.pc);
  regs.pc += 2;
  return r;
}

static inline ULONG nextilong()
{
  ULONG r = (mem.get_word(regs.pc) << 16) + mem.get_word(regs.pc+2);
  regs.pc += 4;
  return r;
}

static effadr GetEA(const addr_mode &a, const ULONG mask)
{
  effadr ea;
  UWORD dp;
  BYTE disp8;
  int r;
  ULONG dispreg;

  ea.szmask = mask;
  switch(a.mode){
  case Dreg:
    ea.addr = a.reg; ea.type = RegD; break;
  case Areg:
    ea.addr = a.reg; ea.type = RegA; break;
  case Aind:
    ea.addr = regs.a[a.reg]; ea.type = Addr; break;
  case Aipi:
    ea.addr = regs.a[a.reg]; regs.a[a.reg] += mask2len(mask);
    ea.type = Addr; break;
  case Apdi:
    ea.addr = regs.a[a.reg] -= mask2len(mask); ea.type = Addr; break;
  case Ad16:
    ea.addr = regs.a[a.reg] + (WORD)nextiword(); // cast for signed rel. values
    ea.type = Addr; break;
  case Ad8r:
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr = regs.a[a.reg] + disp8 + dispreg;
    ea.type = Addr; break;
  case PC16:
    ea.addr = regs.pc;
    ea.addr += (WORD)nextiword(); // cast for signed rel. values
    ea.type = Addr; break;
  case PC8r:
    ea.addr = regs.pc;
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr += disp8 + dispreg;
    ea.type = Addr; break;
  case absw:
    ea.type = Addr;
    ea.addr = nextiword();
    break;
  case absl:
    ea.type = Addr;
    ea.addr = nextilong();
    break;
  case imm:
    switch(mask){
    case 0xff:
      ea.addr = ExtendByte(nextiword()); ea.type = EAIM; break;
    case 0xffff:
      ea.addr = ExtendWord(nextiword()); ea.type = EAIM; break;
    case 0xffffffff:
      ea.addr = nextilong(); ea.type = EAIM; break;
    }
    break;
  case imm3:
    ea.addr = a.reg; ea.type = EAIM; break;
  default:
    abort(); // FIXME
  }
  return ea;
}

static effadr MGetEA(const addr_mode &a, const ULONG mask)
{
  effadr ea;
  UWORD dp;
  BYTE disp8;
  int r;
  ULONG dispreg;

  ea.szmask = mask;
  switch(a.mode){
  case Aind: case Aipi: case Apdi:
    ea.addr = regs.a[a.reg]; ea.type = Addr; break;
  case Ad16:
    ea.addr = regs.a[a.reg] + (WORD)nextiword(); // cast for signed rel. values
    ea.type = Addr; break;
  case Ad8r:
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr = regs.a[a.reg] + disp8 + dispreg;
    ea.type = Addr; break;
  case PC16:
    ea.addr = regs.pc;
    ea.addr += (WORD)nextiword(); // cast for signed rel. values
    ea.type = Addr; break;
  case PC8r:
    ea.addr = regs.pc;
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr += disp8 + dispreg;
    ea.type = Addr; break;
  case absw:
    ea.type = Addr;
    ea.addr = nextiword();
    break;
  case absl:
    ea.type = Addr;
    ea.addr = nextilong();
    break;
  default:
    abort(); // FIXME
  }
  return ea;
}

static void MCompleteEAFromRL(effadr &ea, const addr_mode &a, ULONG mask, UWORD bitmask)
{
  int bitcnt = 0;
  for(int i=0;i<16;i++) {
    bitcnt += bitmask & 1;
    bitmask >>= 1;
  }
  switch(a.mode){
  case Aipi: 
    abort(); break;
  case Apdi:
    ea.addr = regs.a[a.reg] -= bitcnt * mask2len(mask); break;
  default: break;
  }
}

static void MCompleteEAToRL(effadr &ea, const addr_mode &a, ULONG mask, UWORD bitmask)
{
  int bitcnt = 0;
  for(int i=0;i<16;i++) {
    bitcnt += bitmask & 1;
    bitmask >>= 1;
  }
  switch(a.mode){
  case Aipi: 
    regs.a[a.reg] = ea.addr; break;
  case Apdi:
    abort(); break;
  default: break;
  }
}

static ULONG RetrieveEA(const effadr &ea)
{
  switch(ea.type){
  case Addr:
    return ea.addr;
  default:
    abort(); // FIXME
  }
}

static ULONG GetFromEA(const effadr &ea)
{
  switch(ea.type) {
  case RegD:
    return Extend(regs.d[ea.addr],ea.szmask);
  case RegA:
    return Extend(regs.a[ea.addr],ea.szmask);
  case Addr:
    switch(ea.szmask){
    case 0xff:
      return ExtendByte(mem.get_byte(ea.addr));
    case 0xffff:
      return ExtendWord(mem.get_word(ea.addr));
    default: // long
      return (mem.get_word(ea.addr) << 16) + mem.get_word(ea.addr+2);
    }
  case EAIM:
    return ea.addr;
  }
}

static void StoreToEA(const effadr &ea, const ULONG value)
{
  switch(ea.type){
  case RegD:
    regs.d[ea.addr] &= ~ea.szmask; regs.d[ea.addr] |= (value & ea.szmask);
    break;
  case RegA:
    regs.a[ea.addr] = value;
 //   if (ea.szmask == 0xffff) regs.a[ea.addr] = ExtendWord(regs.a[ea.addr]);
    break;
  case Addr:
    switch(ea.szmask){
    case 0xff:
      mem.put_byte(ea.addr,value);
      break;
    case 0xffff:
      mem.put_word(ea.addr,value);
      break;
    default: // long
      mem.put_word(ea.addr,value >> 16); 
      mem.put_word(ea.addr+2, value & 0xffff);
    }
    break;
  default:
    cout << hex << regs.pc;
    abort(); // FIXME
  }
}

static effadr ShowEA(addr_mode a,ULONG mask)
{
  effadr ea;
  UWORD dp;
  BYTE disp8;
  WORD disp16;
  int r;
  ULONG dispreg;

  cout << setw(1) << setfill('0');
  ea.szmask = mask;
  switch(a.mode){
  case Dreg:
    ea.addr = a.reg; ea.type = RegD; 
    cout << "D" << (int)a.reg;
    break;
  case Areg:
    ea.addr = a.reg; ea.type = RegA;
    cout << "A" << (int)a.reg;
    break;
  case Aind:
    ea.addr = regs.a[a.reg]; ea.type = Addr; 
    cout << "(A" << (int)a.reg << ")";
    break;
  case Aipi:
    ea.addr = regs.a[a.reg]; 
    ea.type = Addr; 
    cout << "(A" << (int)a.reg << ")+";
    break;
  case Apdi:
    ea.addr = regs.a[a.reg] - mask2len(mask); ea.type = Addr; 
    cout << "-(A" << (int)a.reg << ")";
    break;
  case Ad16:
    disp16 = nextiword();
    ea.addr = regs.a[a.reg] + (WORD)disp16;
    ea.type = Addr; 
    cout << "(A" << (int)a.reg << ",$" << hex << setw(8) << disp16 << ") == $" 
      << ea.addr << dec;
    break;
  case Ad8r:
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr = regs.a[a.reg] + disp8 + dispreg;
    ea.type = Addr;
    cout << "(A" << (int)a.reg << "," << ((dp & 0x8000) ? "A" : "D") << (int)r
      << ",$" << hex << (int)disp8 << ") == $" << ea.addr
      << dec;
    break;
  case PC16:
    ea.addr = regs.pc;
    disp16 = nextiword();
    ea.addr += (WORD)disp16; // cast for signed rel. values
    ea.type = Addr; 
    cout << "(PC" << ",$" << hex << setw(8) << disp16 << ") == $" << ea.addr 
      << dec;
    break;
  case PC8r:
    ea.addr = regs.pc;
    dp = nextiword();
    disp8 = dp & 0xFF;
    r = (dp & 0x7000) >> 12;
    dispreg = dp & 0x8000 ? regs.a[r] : regs.d[r];

    if (!(dp & 0x800)) dispreg = ExtendWord(dispreg);
    ea.addr += disp8 + dispreg;
    ea.type = Addr; 
    cout << "(PC" << "," << ((dp & 0x8000) ? "A" : "D") << (int)r
      << ",$" << hex << (int)disp8 << ") == $" << ea.addr
      << dec;
    break;
  case absw:
    ea.type = Addr;
    ea.addr = nextiword();
    cout << "$" << hex << ea.addr << dec;
    break;
  case absl:
    ea.type = Addr;
    ea.addr = nextilong();
    cout << "$" << hex << ea.addr << dec;
    break;
  case imm:
    switch(mask){
    case 0xff:
      ea.addr = nextiword() & 0xff; ea.type = EAIM; break;
    case 0xffff:
      ea.addr = nextiword(); ea.type = EAIM; break;
    case 0xffffffff:
      ea.addr = nextilong(); ea.type = EAIM; break;
    }
    cout << "#$" << hex << ea.addr << dec;
    break;
  case imm3:
    ea.addr = a.reg; ea.type = EAIM;
    cout << "#$" << hex << ea.addr << dec;
    break;
  default:
    abort(); // FIXME
  }
  return ea;
}

static inline void setflags_add(const ULONG src, const ULONG oldv, 
                                const ULONG newv)
{
  int s = src >> 31;
  int o = oldv >> 31;
  int n = newv >> 31;

  regs.z = newv == 0;
  regs.c = regs.x = (s && o) || (!n && (o || s));
  regs.n = n;
  regs.v = ((s && o && !n) || (!s && !o && n));
}

static inline void setflags_sub(const ULONG src, const ULONG oldv,
                                const ULONG newv)
{
  int s = src >> 31;
  int o = oldv >> 31;
  int n = newv >> 31;

  regs.z = newv == 0;
  regs.c = regs.x = (s && !o) || (n && (!o || s));
  regs.n = n;
  regs.v = ((!s && o && !n) || (s && !o && n));
}

static inline void setflags_cmp(const ULONG src, const ULONG oldv, 
				const ULONG newv)
{
  int sh = 31; //mask2shift(mask);
  int s = (src >> sh); // & 1;
  int o = (oldv >> sh); // & 1;
  int n = newv >> sh;

  regs.z = newv == 0;
  regs.c = (s && !o) || (n && (!o || s));
  regs.n = n;
  regs.v = ((!s && o && !n) || (s && !o && n));
}

static inline void setflags_logical(ULONG newv, const ULONG mask)
{
  newv &= mask;
  regs.z = newv == 0;
  regs.n = newv >> mask2shift(mask);
  regs.v = regs.c = 0;
}

static bool cctrue(const int cc)
{
  switch(cc){
  case 0: return true;                         // T
  case 1: return false;                        // F
  case 2: return !regs.c && !regs.z;           // HI
  case 3: return regs.c || regs.z;             // LS
  case 4: return !regs.c;                      // CC
  case 5: return regs.c;                       // CS
  case 6: return !regs.z;                      // NE
  case 7: return regs.z;                       // EQ
  case 8: return !regs.v;                      // VC
  case 9: return regs.v;                       // VV
  case 10:return !regs.n;                      // PL
  case 11:return regs.n;                       // MI
  case 12:return regs.n == regs.v;             // GE
  case 13:return regs.n != regs.v;             // LT
  case 14:return !regs.z && (regs.n == regs.v);// GT
  case 15:return regs.z || (regs.n != regs.v); // LE
  }
  abort();
  return 0;
}

static void SuperState()
{
  if (!regs.s){
    regs.s = 1; 
    regs.t = 0;
    CPTR temp = regs.usp;
    regs.usp = regs.a[7];
    regs.a[7] = temp;
  }
}

static void UserState()
{
  if (regs.s){
    regs.s = 0; 
 //   regs.t = 0;
    CPTR temp = regs.usp;
    regs.usp = regs.a[7];
    regs.a[7] = temp;
  }
}

static void MakeSR()
{
  regs.sr = (regs.t << 15) | (regs.s << 13) | (regs.intmask << 8)
    | (regs.x << 4) | (regs.n << 3) | (regs.z << 2) | (regs.v << 1) 
      |  regs.c;
}

static void MakeFromSR()
{
  int olds = regs.s;

  regs.t = (regs.sr >> 15) & 1;
  regs.s = (regs.sr >> 13) & 1;
  regs.intmask = (regs.sr >> 8) & 7;
  regs.x = (regs.sr >> 4) & 1;
  regs.n = (regs.sr >> 3) & 1;
  regs.z = (regs.sr >> 2) & 1;
  regs.v = (regs.sr >> 1) & 1;
  regs.c = regs.sr & 1;
  if (olds != regs.s) {
    cout << "Mode switch!\n";
    regs.s = olds; 
    if (regs.s) UserState(); else SuperState();
  }
}

static void Exception(int nr, int newimask=-1)
{
  MakeSR();
  SuperState();
  if (newimask != -1) regs.intmask = newimask;
  addr_mode pda7(Apdi,7);
  effadr lea = GetEA(pda7,0xffffffff);
  effadr wea = GetEA(pda7,0xffff);
  StoreToEA(wea,regs.sr);
  StoreToEA(lea,regs.pc);
  regs.pc = (mem.get_word(4*nr) << 16) + mem.get_word(4*nr+2);
}

static void Interrupt(int nr)
{
  assert(nr < 8 && nr >= 0);
 //  regs.intmask = nr;
  Exception(nr+24,nr);
}

 /*
  * instruction emulations
  */

static instr_result INST_ILLG(const instr_params &)
{
  regs.pc -= 2;
  
  Exception(4);
}

static instr_result INST_NIMP(const instr_params &p)
{
  cout << "unimplemented (020 ??) instruction: " 
    << mem.get_word(regs.pc-2) << "\n";
  INST_ILLG(p);
}

static instr_result INST_OR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  dstv |= srcv;
  setflags_logical(dstv, p.mask);
  StoreToEA(dst,dstv);
}

static instr_result INST_AND(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  dstv &= srcv;
  setflags_logical(dstv, p.mask);
  StoreToEA(dst,dstv);
}

static instr_result INST_EOR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  dstv ^= srcv;
  setflags_logical(dstv, p.mask);
  StoreToEA(dst,dstv);
}

static instr_result INST_CMP(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv - srcv;
  setflags_cmp(srcv, dstv, newv);
}

static instr_result INST_ADD(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv + srcv;
  setflags_add(srcv, dstv, newv);
  StoreToEA(dst,newv);
}

static instr_result INST_SUB(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv - srcv;
  setflags_sub(srcv, dstv, newv);
  StoreToEA(dst,newv);
}

static instr_result INST_ADDX(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv + srcv + regs.x;
  setflags_add(srcv, dstv, newv);
  StoreToEA(dst,newv);
}

static instr_result INST_SUBX(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv - srcv - regs.x;
  setflags_sub(srcv, dstv, newv);
  StoreToEA(dst,newv);
}

static instr_result INST_ADDA(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, 0xffffffff);
  ULONG dstv = GetFromEA(dst);
//  if (src.szmask == 0xffff) srcv = ExtendWord(srcv);
  StoreToEA(dst,dstv + srcv);
}

static instr_result INST_SUBA(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, 0xffffffff);
  ULONG dstv = GetFromEA(dst);
//  if (src.szmask == 0xffff) srcv = ExtendWord(srcv);
  StoreToEA(dst,dstv - srcv);
}

static instr_result INST_CMPA(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, 0xffffffff);
  ULONG dstv = GetFromEA(dst);
  ULONG newv = dstv - srcv;
  setflags_cmp(srcv, dstv, newv);
}

static instr_result INST_CHK(const instr_params &p)
{
  cout << "CHK occurred\n";
  INST_ILLG(p); // FIXME
}

static instr_result INST_EXGL(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG dstv = GetFromEA(dst);
  StoreToEA(dst,srcv);
  StoreToEA(src,dstv);  
}

static instr_result INST_LEA(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  StoreToEA(dst,RetrieveEA(src));
}

static instr_result INST_MOVE(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  setflags_logical(srcv,p.mask);
  StoreToEA(dst,srcv);
}

static instr_result INST_MOVEQ(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG srcv = ExtendByte(GetFromEA(src));
  setflags_logical(srcv,p.mask);
  StoreToEA(dst,srcv);
}

static instr_result INST_MOVEA(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  StoreToEA(dst,srcv);
}

static instr_result INST_BTST(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.dest.mode == Dreg ? 0xffffffff : 0xff);
  ULONG dstv = GetFromEA(dst);
  if (p.dest.mode == Dreg) {
    srcv &= 31;
  } else {
    srcv &= 7; 
  }
  regs.z = !(dstv & (1 << srcv));
}

static instr_result INST_BCLR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.dest.mode == Dreg ? 0xffffffff : 0xff);
  ULONG dstv = GetFromEA(dst);
  if (p.dest.mode == Dreg) {
    srcv &= 31;
  } else {
    srcv &= 7; 
  }
  regs.z = !(dstv & (1 << srcv));
  StoreToEA(dst,dstv & ~(1 << srcv));
}

static instr_result INST_BSET(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.dest.mode == Dreg ? 0xffffffff : 0xff);
  ULONG dstv = GetFromEA(dst);
  if (p.dest.mode == Dreg) {
    srcv &= 31; 
  } else {
    srcv &= 7; 
  }
  regs.z = !(dstv & (1 << srcv));
  StoreToEA(dst,dstv | (1 << srcv));
}

static instr_result INST_BCHG(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  ULONG srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.dest.mode == Dreg ? 0xffffffff : 0xff);
  ULONG dstv = GetFromEA(dst);
  if (p.dest.mode == Dreg) {
    srcv &= 31; 
  } else {
    srcv &= 7; 
  }
  regs.z = !(dstv & (1 << srcv));
  StoreToEA(dst,dstv ^ (1 << srcv));
}

static instr_result INST_NEG(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  ULONG newv = -val;
  setflags_sub(val, 0, newv);
  StoreToEA(ea,newv);
}

static instr_result INST_NEGX(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  ULONG newv = -val-regs.x;
  setflags_sub(val, 0, newv);
  StoreToEA(ea,newv);
}

static instr_result INST_NOT(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  ULONG newv = ~val;
  setflags_logical(newv,p.mask);
  StoreToEA(ea,newv);
}

static instr_result INST_CLR(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);        // not a bug, a feature!
  ULONG newv = 0;
  setflags_logical(newv,p.mask);
  StoreToEA(ea,newv);
}

static instr_result INST_TST(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  setflags_logical(val,p.mask);
}

static instr_result INST_SWAP(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  val = (val >> 16) | (val << 16);
  StoreToEA(ea, val);
  setflags_logical(val,p.mask);
}

static instr_result INST_EXTW(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  StoreToEA(ea, ExtendByte(val));
}

static instr_result INST_EXTL(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  ULONG val = GetFromEA(ea);
  StoreToEA(ea, ExtendWord(val));
}

static instr_result INST_Scc(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  StoreToEA(ea,cctrue(p.dest.reg) ? 0xff : 0);
}

static instr_result INST_ABCD(const instr_params &p)
{
  abort(); // FIXME
}

static instr_result INST_SBCD(const instr_params &p)
{
  abort(); // FIXME
}

static instr_result INST_NBCD(const instr_params &p)
{
  abort(); // FIXME
}

static instr_result INST_NOP(const instr_params &)
{
  // :-D
}

static instr_result INST_RESET(const instr_params &)
{
  customreset();
}

static instr_result INST_STOP(const instr_params &p)
{
  if ((p.mask != 0xff) && !regs.s) {
    regs.pc -= 2;
    Exception(8);
  } else {
    effadr ea = GetEA(p.src, p.mask);
    regs.sr = GetFromEA(ea);
    MakeFromSR();
    regs.stopped = 1;
  }
}

static instr_result INST_TRAPV(const instr_params &)
{
  if (regs.v) Exception(7);
}

static instr_result INST_TRAP(const instr_params &p)
{
  UWORD val = GetFromEA(GetEA(p.src,p.mask));
  Exception(val+32);
}

static instr_result INST_BSRB(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  BYTE offset = GetFromEA(ea);
  addr_mode pda7(Apdi,7);
  effadr stackp = GetEA(pda7,0xffffffff);
  StoreToEA(stackp,regs.pc);
  regs.pc += offset;
}

static instr_result INST_BccB(const instr_params &p)
{
  if (cctrue(p.dest.reg)){
    effadr ea = GetEA(p.src, p.mask);
    BYTE offset = GetFromEA(ea);
    regs.pc += offset;
  }
}

static instr_result INST_BSRW(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  WORD offset = GetFromEA(ea);
  addr_mode pda7(Apdi,7);
  effadr stackp = GetEA(pda7,0xffffffff);
  StoreToEA(stackp,regs.pc);
  regs.pc += offset - 2;
}

static instr_result INST_BccW(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  WORD offset = GetFromEA(ea);
  if (cctrue(p.dest.reg)){
    regs.pc += offset - 2;
  }
}

static instr_result INST_DBcc(const instr_params &p)
{
  WORD offset = nextiword();

  if (!cctrue(p.dest.reg)){
    effadr ea = GetEA(p.src, p.mask); // safe inside if, can only be Dreg
    ULONG dr = GetFromEA(ea);
    if (dr--){
      regs.pc += offset - 2;
    }
    StoreToEA(ea,dr);
  }
}

static instr_result INST_JMP(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  regs.pc = RetrieveEA(ea);
}

static instr_result INST_JSR(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);

  addr_mode pda7(Apdi,7);
  effadr stackp = GetEA(pda7,0xffffffff);
  StoreToEA(stackp, regs.pc);

  regs.pc = RetrieveEA(ea);
}

static instr_result INST_RTE(const instr_params &)
{
  addr_mode a7pi(Aipi,7);
  effadr stackp = GetEA(a7pi,0xffff);
  regs.sr = GetFromEA(stackp);
  stackp = GetEA(a7pi,0xffffffff);
  regs.pc = GetFromEA(stackp);
  MakeFromSR();
}

static instr_result INST_RTD(const instr_params &p)
{
  INST_NIMP(p); // FIXME
}

static instr_result INST_RTS(const instr_params &)
{
  addr_mode a7pi(Aipi,7);
  effadr stackp = GetEA(a7pi,0xffffffff);
  regs.pc = GetFromEA(stackp);
}

static instr_result INST_RTR(const instr_params &)
{
  MakeSR();
  regs.sr &= 0xff00;
  addr_mode a7pi(Aipi,7);
  effadr stackp = GetEA(a7pi,0xffff);
  regs.sr |= GetFromEA(stackp) & 0xff;
  stackp = GetEA(a7pi,0xffffffff);
  regs.pc = GetFromEA(stackp);
  MakeFromSR();
}

static instr_result INST_LINK(const instr_params &p)
{
  addr_mode pda7(Apdi,7);
  effadr stackp = GetEA(pda7,0xffffffff);
  effadr ar = GetEA(p.src, 0xffffffff);
  StoreToEA(stackp, GetFromEA(ar));
  StoreToEA(ar, regs.a[7]);
  effadr ea = GetEA(p.dest, p.mask);
  ULONG offset = GetFromEA(ea);
//  if (p.mask = 0xffff) offset = ExtendWord(offset);
  regs.a[7] += offset;
}

static instr_result INST_UNLK(const instr_params &p)
{
  effadr ar = GetEA(p.src, 0xffffffff);
  regs.a[7] = GetFromEA(ar);

  addr_mode a7pi(Aipi,7);
  effadr stackp = GetEA(a7pi,0xffffffff);
  StoreToEA(ar, GetFromEA(stackp));
}

static instr_result INST_MULU(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  UWORD srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  UWORD dstv = GetFromEA(dst);
  ULONG newv = (ULONG)dstv * (ULONG)srcv;
  setflags_logical(newv, 0xffffffff);
  dst.szmask = 0xffffffff;
  StoreToEA(dst,newv);
}

static instr_result INST_MULS(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  WORD srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, p.mask);
  WORD dstv = GetFromEA(dst);
  ULONG newv = (LONG)dstv * (LONG)srcv;
  setflags_logical(newv, 0xffffffff);
  dst.szmask = 0xffffffff;
  StoreToEA(dst,newv);
}

static instr_result INST_DIVU(const instr_params &p)
{
  effadr src = GetEA(p.src, 0xffff);
  UWORD srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, 0xffffffff);
  ULONG dstv = GetFromEA(dst);
  if (srcv == 0){
    // FOO !
  } else {
    ULONG newv = dstv / srcv;
    UWORD rem = dstv % srcv;
    setflags_logical(newv, 0xffff);
    regs.v = newv > 0xffff;
    newv = (newv & 0xffff) | (rem << 16);
    StoreToEA(dst,newv);
  }
}

static instr_result INST_DIVS(const instr_params &p)
{
  effadr src = GetEA(p.src, 0xffff);
  WORD srcv = GetFromEA(src);
  effadr dst = GetEA(p.dest, 0xffffffff);
  LONG dstv = GetFromEA(dst);
  if (srcv == 0){
    // FOO !
  } else {
    LONG newv = dstv / srcv;
    UWORD rem = dstv % srcv;
    if ((rem & 0x8000) != (newv & 0x8000)) rem = -rem;
    setflags_logical(newv, 0xffff);
    regs.v = (newv & 0xffff0000) && (newv & 0xffff0000) != 0xffff0000;
    newv = (newv & 0xffff) | (rem << 16);
    StoreToEA(dst,newv);
  }
}

static instr_result INST_ASL(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG sign = data & cmask;
  regs.v = 0;
  for (;cnt;--cnt){
    regs.c = regs.x = (data & cmask) != 0;
    data <<= 1;
    if ((data & cmask) != sign) regs.v = 1; 
  }
  regs.n = (data & cmask) != 0;
  regs.z = (data & p.mask) == 0;
  StoreToEA(dst,data);
}

static instr_result INST_ASR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG sign = data & cmask;
  regs.v = 0;
  for (;cnt;--cnt){
    regs.c = regs.x = data & 1;
    data = (data >> 1) | sign;
  }
  regs.n = sign != 0;
  regs.z = (data & p.mask) == 0;
  StoreToEA(dst,data);
}

static instr_result INST_LSR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask; 

  int carry = 0;
  for (;cnt;--cnt){
    carry = data & 1;
    data >>= 1;
  }
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry;
  StoreToEA(dst,data);
}

static instr_result INST_LSL(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG carry = 0;

  for (;cnt;--cnt){
    carry = data & cmask;
    data <<= 1;
  }
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_ROR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  int carry = 0;
  for (;cnt;--cnt){
    carry = data & 1;
    data >>= 1;
    if (carry) data |= cmask;
  }
  setflags_logical(data,p.mask);
  regs.c = carry;
  StoreToEA(dst,data);
}

static instr_result INST_ROL(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG carry = 0;

  for (;cnt;--cnt){
    carry = data & cmask;
    data <<= 1;
    if (carry) data |= 1;
  }
  setflags_logical(data,p.mask);
  regs.c = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_ROXR(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  int carry = 0;
  for (;cnt;--cnt){
    carry = data & 1;
    data >>= 1;
    if (regs.x) data |= cmask;
    regs.x = carry;
  }
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry;
  StoreToEA(dst,data);
}

static instr_result INST_ROXL(const instr_params &p)
{
  effadr src = GetEA(p.src, p.mask);
  effadr dst = GetEA(p.dest, p.mask);
  ULONG cnt = GetFromEA(src) & 31;
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  int carry = 0;

  for (;cnt;--cnt){
    carry = data & cmask;
    data <<= 1;
    if (regs.x) data |= 1;
    regs.x = carry != 0;
  }
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_ASL1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG sign = data & cmask;
  regs.v = 0;
  regs.c = regs.x = sign != 0;
  data <<= 1;
  if ((data & cmask) != sign) regs.v = 1; 
  regs.n = (data & cmask) != 0;
  regs.z = (data & p.mask) == 0;
  StoreToEA(dst,data);
}

static instr_result INST_ASR1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG sign = data & cmask;
  regs.v = 0;
  regs.c = regs.x = data & 1;
  data = (data >> 1) | sign;
  regs.n = sign != 0;
  regs.z = (data & p.mask) == 0;
  StoreToEA(dst,data);
}

static instr_result INST_LSR1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;
  int carry = data & 1;
  data >>= 1;
  setflags_logical(data, p.mask);
  regs.c = regs.x = carry;
  StoreToEA(dst,data);
}

static instr_result INST_LSL1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG carry = data & cmask;
  data <<= 1;
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_ROR1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  int carry = data & 1;
  data >>= 1;
  if (carry) data |= cmask;
  setflags_logical(data,p.mask);
  regs.c = carry;
  StoreToEA(dst,data);
}

static instr_result INST_ROL1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG carry = data & cmask;

  data <<= 1;
  if (carry) data |= 1;
  setflags_logical(data,p.mask);
  regs.c = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_ROXR1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  int carry = data & 1;
  data >>= 1;
  if (regs.x) data |= cmask;
//  regs.x = carry;
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry;
  StoreToEA(dst,data);
}

static instr_result INST_ROXL1(const instr_params &p)
{
  effadr dst = GetEA(p.dest, p.mask);
  ULONG data = GetFromEA(dst) & p.mask;

  ULONG cmask = 1 << mask2shift(p.mask);
  ULONG carry = data & cmask;

  data <<= 1;
  if (regs.x) data |= 1;
//  regs.x = carry != 0;
  
  setflags_logical(data,p.mask);
  regs.c = regs.x = carry != 0;
  StoreToEA(dst,data);
}

static instr_result INST_MOVECCREA(const instr_params &p)
{
  INST_NIMP(p); // FIXME
}

static instr_result INST_MOVESREA(const instr_params &p)
{
  effadr ea = GetEA(p.src, p.mask);
  MakeSR();
  StoreToEA(ea,regs.sr);
}

static instr_result INST_MOVEEACCR(const instr_params &p)
{
  MakeSR();
  regs.sr &= 0xFF00;
  effadr ea = GetEA(p.src, p.mask);
  regs.sr |= GetFromEA(ea) & 0xFF;
  MakeFromSR();
}

static instr_result INST_MOVEEASR(const instr_params &p)
{
  if ((p.mask != 0xff) && !regs.s) {
    regs.pc -= 2;
    Exception(8);
  } else {
    effadr ea = GetEA(p.src, p.mask);
    regs.sr = GetFromEA(ea);
    MakeFromSR();
  }
}

static instr_result INST_ORSR(const instr_params &p)
{
  if ((p.mask != 0xff) && !regs.s) {
    regs.pc -= 2;
    Exception(8);
  } else {
    MakeSR();
    effadr ea = GetEA(p.src, p.mask);
    regs.sr |= GetFromEA(ea);
    MakeFromSR();
  }
}

static instr_result INST_EORSR(const instr_params &p)
{
  if ((p.mask != 0xff) && !regs.s) {
    regs.pc -= 2;
    Exception(8);
  } else {
    MakeSR();
    effadr ea = GetEA(p.src, p.mask);
    regs.sr ^= GetFromEA(ea);
    MakeFromSR();
  }
}

static instr_result INST_ANDSR(const instr_params &p)
{
  if ((p.mask != 0xff) && !regs.s) {
    regs.pc -= 2;
    Exception(8);
  } else {
    MakeSR();
    effadr ea = GetEA(p.src, p.mask);
    regs.sr &= GetFromEA(ea);
    MakeFromSR();
  }
}

static instr_result INST_MOVEMEARL(const instr_params &p)
{
  int i;
  UWORD bitmask = nextiword();
  effadr ea = MGetEA(p.src, p.mask);
  for(i=0; i<8; i++) {
    if (bitmask & (1 << i)) {
      regs.d[i] = (regs.d[i] & ~p.mask) | (GetFromEA(ea) & p.mask);
      ea.addr += mask2len(p.mask);
    }
  }
  for(i=0; i<8; i++) {
    if (bitmask & (1 << (i + 8))) {
      regs.a[i] = Extend(GetFromEA(ea),p.mask);
      ea.addr += mask2len(p.mask);
    }
  }
  MCompleteEAToRL(ea, p.src, p.mask, bitmask);
}

static instr_result INST_MOVEMRLEA(const instr_params &p)
{
  int rd[8], ra[8], i, kludge;
  for(i=0;i<8;i++){
    rd[i] = regs.d[i]; ra[i] = regs.a[i];
  }
  UWORD bitmask = nextiword();
  effadr ea = MGetEA(p.src, p.mask);
  kludge = (p.src.mode == Apdi) ? 15 : 0;
  MCompleteEAFromRL(ea, p.src, p.mask, bitmask);
  for(i=0; i<8; i++) {
    if (bitmask & (1 << (i ^ kludge))) {
      StoreToEA(ea,rd[i]);
      ea.addr += mask2len(p.mask);
    }
  }
  for(i=0; i<8; i++) {
    if (bitmask & (1 << ((i + 8) ^ kludge))) {
      StoreToEA(ea,ra[i]);
      ea.addr += mask2len(p.mask);
    }
  }
}

static instr_result INST_MOVEAUSP(const instr_params &p)
{
  effadr ea = GetEA(p.src,0xffffffff);
  if (regs.s) {
    regs.usp = GetFromEA(ea);
  } else {
    regs.pc -= 2; Exception(8);
  }
}

static instr_result INST_MOVEUSPA(const instr_params &p)
{
  effadr ea = GetEA(p.src,0xffffffff);
  if (regs.s) {
    StoreToEA(ea,regs.usp);
  } else {
    regs.pc -= 2; Exception(8);
  }
}

static instr_result INST_LINEA(const instr_params &)
{
  regs.pc -= 2;
  
  Exception(10);
}

static instr_result INST_LINEF(const instr_params &)
{
  regs.pc -= 2;
  
  Exception(11);
}

 /*
  * debugging functions
  */

struct debug_entry {
  instr_func f;
  char *name;
  bool src,dest,spc;
};

static debug_entry debugtbl[] = {
  { &INST_OR,"OR",1,1,0 },
  { &INST_AND,"AND",1,1,0 },
  { &INST_EOR,"EOR",1,1,0 },
  { &INST_CMP,"CMP",1,1,0 },
  { &INST_ADD,"ADD",1,1,0 },
  { &INST_SUB,"SUB",1,1,0 },
  { &INST_ADDX,"ADDX",1,1,0 },
  { &INST_SUBX,"SUBX",1,1,0 },

  { &INST_ADDA,"ADDA",1,1,0 },
  { &INST_SUBA,"SUBA",1,1,0 },
  { &INST_CMPA,"CMPA",1,1,0 },

  { &INST_CHK,"CHK",1,1,0 },

  { &INST_EXGL,"EXG",1,1,0 },

  { &INST_LEA,"LEA",1,1,0 },
  { &INST_MOVE,"MOVE",1,1,0 },
  { &INST_MOVEA,"MOVEA",1,1,0 },
  { &INST_MOVEQ,"MOVEQ",1,1,0 },
  { &INST_BTST,"BTST",1,1,0 },
  { &INST_BCLR,"BCLR",1,1,0 },
  { &INST_BSET,"BSET",1,1,0 },
  { &INST_BCHG,"BCHG",1,1,0 },
  { &INST_NEG,"NEG",1,0,0 },
  { &INST_NEGX,"NEGX",1,0,0 },
  { &INST_NOT,"NOT",1,0,0 },
  { &INST_CLR,"CLR",1,0,0 },
  { &INST_TST,"TST",1,0,0 },
  { &INST_SWAP,"SWAP",1,0,0 },
  { &INST_EXTW,"EXT",1,0,0 },
  { &INST_EXTL,"EXT",1,0,0 },
  { &INST_Scc,"Scc",1,0,0 },
  { &INST_ABCD,"ABCD",1,1,0 },
  { &INST_SBCD,"SBCD",1,1,0 },
  { &INST_NBCD,"NBCD",1,1,0 },
  { &INST_NOP,"NOP",0,0,0 },
  { &INST_RESET,"RESET",0,0,0 },
  { &INST_STOP,"STOP",1,0,0 },
  { &INST_TRAPV,"TRAPV",1,0,0 },
  { &INST_TRAP,"TRAP",1,0,0 },
  { &INST_BSRB,"BSR",1,0,0 },
  { &INST_BccB,"Bcc",1,0,0 },
  { &INST_BSRW,"BSR",1,0,0 },
  { &INST_BccW,"Bcc",1,0,0 },
  { &INST_DBcc,"DBcc",1,0,10 },
  { &INST_JMP,"JMP",1,0,0 },
  { &INST_JSR,"JSR",1,0,0 },
  { &INST_RTE,"RTE",0,0,0 },
  { &INST_RTD,"RTD",0,0,0 },
  { &INST_RTS,"RTS",0,0,0 },
  { &INST_RTR,"RTR",0,0,0 },
  { &INST_LINK,"LINK",1,1,0 },
  { &INST_UNLK,"UNLK",1,0,0 },
  { &INST_MULU,"MULU",1,1,0 },
  { &INST_MULS,"MULS",1,1,0 },
  { &INST_DIVU, "DIVU",1,1,0 },
  { &INST_DIVS,"DIVS",1,1,0 },
  { &INST_ASR,"ASR",1,1,0 },
  { &INST_ASL,"ASL",1,1,0 },
  { &INST_LSR,"LSR",1,1,0 },
  { &INST_LSL,"LSL",1,1,0 },
  { &INST_ROR,"ROR",1,1,0 },
  { &INST_ROL,"ROL",1,1,0 },
  { &INST_ROXR,"ROXR",1,1,0 },
  { &INST_ROXL,"ROXL",1,1,0 },
  { &INST_ASR1,"ASR",1,0,0 },
  { &INST_ASL1,"ASL",1,0,0 },
  { &INST_LSR1,"LSR",1,0,0 },
  { &INST_LSL1,"LSL",1,0,0 },
  { &INST_ROR1,"ROR",1,0,0 },
  { &INST_ROL1,"ROL",1,0,0 },
  { &INST_ROXR1,"ROXR",1,0,0 },
  { &INST_ROXL1,"ROXL",1,0,0 },
  { &INST_MOVECCREA,"MOVE CCR,",1,0,0 },
  { &INST_MOVESREA,"MOVE SR,",1,0,0 },
  { &INST_MOVEEACCR,"MOVE2CCR",1,0,0 },
  { &INST_MOVEEASR,"MOVE2SR",1,0,0 },
  { &INST_MOVEMEARL,"MOVEM",1,0,9 },
  { &INST_MOVEMRLEA,"MOVEM",1,0,5 },
  { &INST_MOVEAUSP,"MOVE2USP",1,1,0 },
  { &INST_MOVEUSPA,"MOVE USP,",1,1,0 },

  { &INST_ORSR,"ORSR",1,0,0 },
  { &INST_ANDSR,"ANDSR",1,0,0 },
  { &INST_EORSR,"EORSR",1,0,0 },

  { &INST_LINEA,"LINEA",0,0,0 },
  { &INST_LINEF,"LINEF",0,0,0 },
  { &INST_ILLG,"ILLEGAL",0,0,0 },
  { &INST_NIMP,"NOT IMPLEMENTED",0,0,0 },
  { NULL,NULL,0,0,0 }
};

static char* ccnames[] =
{ "T ","F ","HI","LS","CC","CS","NE","EQ",
  "VC","VS","PL","MI","GE","LT","GT","LE" };

void MC68000_reset()
{
  cout << "RESET!\n";
  regs.a[7] = (mem.get_word(0x00f80000) << 16) + mem.get_word(0xf80002);
  regs.pc = (mem.get_word(0x00f80004) << 16) + mem.get_word(0xf80006);
  regs.s = 1;
  regs.stopped = 0;
  instr_params p;
  INST_RESET(p);
}

void MC68000_step()
{
  if (!regs.stopped) {
    UWORD opcode = nextiword();
    I_dec_tab_entry &dtentry = instr_dectab[opcode];
    dtentry.execfunc(dtentry.params);
  }
  for(int i=0;i<4;i++) clockcycle();
  int intr = intlev();
  if (intr != -1 && intr > regs.intmask) {
    Interrupt(intr);
    regs.stopped = 0;
  }
}

void MC68000_skip(CPTR nextpc)
{
  do {
    MC68000_step();
  } while (nextpc != regs.pc);
}

void MC68000_disasm(CPTR addr,CPTR &nextpc,int cnt)
{

  CPTR pc = regs.pc;
  regs.pc = addr;
  for (;cnt--;){
    cout << hex << setfill('0');
    char instrname[20],*ccpt;
    int opwords;
    cout << setw(8) << regs.pc << ": ";
    for(opwords=0;opwords<5;opwords++){
      cout << setw(4) << mem.get_word(regs.pc + opwords*2) << " ";
    }
    cout << dec;

    UWORD opcode = nextiword();
    I_dec_tab_entry &dtentry = instr_dectab[opcode];
    debug_entry *dp;
    for (dp = debugtbl;dp->f && dp->f != dtentry.execfunc;dp++)
      {}

    if (!dp->f) {
      cout << "PANIC OPCODE " << opcode << " AT " << addr;
      abort();
    }
    strcpy(instrname,dp->name);
    ccpt = strstr(instrname,"cc");
    if (ccpt != 0) {
      strncpy(ccpt,ccnames[dtentry.params.dest.reg],2);
    }
    cout << instrname;
    switch(dtentry.params.mask){
    case 0xff: cout << ".B "; break;
    case 0xffff: cout << ".W "; break;
    case 0xffffffff: cout << ".L "; break;
    }
    UWORD special = 0;
    if (dp->spc & 1) {
      special = nextiword();
    }
    if (dp->spc & 4) {
      cout << "#$" << hex << setw(4) << special << ",";
    }
    if (dp->src) {
      ShowEA(dtentry.params.src,dtentry.params.mask);
    }
    if (dp->src && dp->dest) cout << ",";
    if (dp->dest) {
      ShowEA(dtentry.params.dest,dtentry.params.mask);
    }
    if (dp->spc & 2) {
      special = nextiword();
    }
    if (dp->spc & 8) {
      cout << ",#$" << hex << setw(4) << special;
    }
    if (ccpt != 0) {
      cout << ( (cctrue(dtentry.params.dest.reg)) ? " (TRUE)" : " (FALSE)");
    }
    cout << "\n";
  }
  nextpc = regs.pc;
  regs.pc = pc;
}

void MC68000_dumpstate(CPTR &nextpc)
{
  cout << hex << setfill('0');

  int i;
  for(i=0;i<8;i++){
    cout << "D" << setw(1) << i << ": " << setw(8) << regs.d[i] << " ";
    if ((i & 3) == 3) cout << "\n";
  }
  for(i=0;i<8;i++){
    cout << "A" << setw(1) << i << ": "  << setw(8) << regs.a[i] << " ";
    if ((i & 3) == 3) cout << "\n";
  }
  cout << setw(1);
  cout << "T=" << regs.t << " S=" << regs.s << " X=" << regs.x << " N=" 
    << regs.n << " Z=" << regs.z << " V=" << regs.v << " C=" << regs.c 
      << " IMASK=" << regs.intmask << "\n";
  MC68000_disasm(regs.pc,nextpc);
  cout << "next PC: " << setw(8) << hex << nextpc << "\n";
}

 /* 
  * GenerateDecTab() creates the instruction decode table.
  * FIXME: This should generate all legal instructions, but some illegal ones
  * will not be directed to INST_ILLG. On an accidental execution, a function
  * like GetEA() may call abort().
  *
  * This function is a monster and should probably be changed.
  */

static void GenerateDecTab()
{
  int offset;
  for(long int opcode=0;opcode<65536;opcode++){
    int r1 = (opcode & 0x0E00) >> 9;
    int m1 = (opcode & 0x01C0) >> 6;
    int m2 = (opcode & 0x0038) >> 3;
    int r2 = (opcode & 0x0007);
    int x  = (m1 & 4) >> 2;
    int sz = (m1 & 3);
    addr_mode am1(m1,r1),am2(m2,r2);
    I_dec_tab_entry entry;

    switch(opcode & 0xF000) {
    case 0x0000: 
      if (x) { 
	if (am2.mode == Dreg || am2.mode == Aind ||
	    am2.mode == Aipi || am2.mode == Apdi ||
	    am2.mode == Ad16 || am2.mode == Ad8r ||
	    am2.mode == absw || am2.mode == absl) {
	  switch(sz){ 
	  case 0:
	    entry.execfunc = &INST_BTST;
	    break;
	  case 1:
	    entry.execfunc = &INST_BCHG;
	    break;
	  case 2:
	    entry.execfunc = &INST_BCLR;
	    break;
	  case 3:
	    entry.execfunc = &INST_BSET;
	    break;
	  }
	  entry.params.mask = 0xffffffff;
	  am1 = am2;
	  am2.mode = Dreg; am2.reg = r1;
	} else {
	  entry.execfunc = &INST_NIMP; // MOVEP
	}
      } else {
	am1.mode = imm;
	if (sz == 3) {
	  switch(r1){
	  case 0:
	  case 1:
	  case 2:
	  case 6:
	  case 7:
	    entry.execfunc = &INST_ILLG;
	    break;
	  case 3:
	    entry.execfunc = &INST_NIMP; // RTM
	    break;
	  case 4:
	    entry.execfunc = &INST_BSET;
	    am1 = am2;
	    am2.mode = imm;
	    entry.params.mask = 0xffff;
	    break;
	  case 5:
	    entry.execfunc = &INST_NIMP; // CAS
	    break;
	  }
	} else {
	  entry.params.mask = sizemask(sz);
	  switch(r1){
	  case 0:
	    if (am2.mode == imm) 
	      entry.execfunc = &INST_ORSR;
	    else
	      entry.execfunc = &INST_OR;
	    break;
	  case 1:
	    if (am2.mode == imm) 
	      entry.execfunc = &INST_ANDSR;
	    else
	      entry.execfunc = &INST_AND;
	    break;
	  case 2:
	    entry.execfunc = &INST_SUB;
	    break;
	  case 3:
	    entry.execfunc = &INST_ADD;
	    break;
	  case 4:
	    switch(sz){
	    case 0: entry.execfunc = &INST_BTST; break;
	    case 1: entry.execfunc = &INST_BCHG; break;
	    case 2: entry.execfunc = &INST_BCLR; break;
	    }
	    entry.params.mask = 0xffff;
	    break;
	  case 5:
	    if (am2.mode == imm) 
	      entry.execfunc = &INST_EORSR;
	    else
	      entry.execfunc = &INST_EOR;
	    break;
	  case 6:
	    entry.execfunc = &INST_CMP;
	    break;
	  case 7:
	    entry.execfunc = &INST_NIMP; // MOVES
	    break;
	  }
	  am1 = am2; am2.mode = imm;
	}
      }
      break; 
    case 0x1000:
      switch(am1.mode) {
      case Areg: case imm: case imm3: case ill2: case ill3:
	entry.execfunc = &INST_ILLG; // MOVEA.B ???
	break;
      default:
	entry.execfunc = &INST_MOVE; // MOVE.B
	break;
      }
      entry.params.mask = 0xff;
      break; 
    case 0x2000:
      switch(am1.mode) {
      case Areg:
	entry.execfunc = &INST_MOVEA; // MOVEA.L
	break;
      case imm: case imm3: case ill2: case ill3:
	entry.execfunc = &INST_ILLG;
	break;
      default:
	entry.execfunc = &INST_MOVE; // MOVE.L
	break;
      }
      entry.params.mask = 0xffffffff; 
      break; 
    case 0x3000: 
      switch(am1.mode) {
      case Areg:
	entry.execfunc = &INST_MOVEA; // MOVEA.W
	break;
      case imm: case imm3: case ill2: case ill3:
	entry.execfunc = &INST_ILLG;
	break;
      default:
	entry.execfunc = &INST_MOVE; // MOVE.W
	break;
      }
      entry.params.mask = 0xffff;
      break; 
    case 0x4000: 
      if (x){
	switch(sz){
	case 0:
	  entry.execfunc = &INST_CHK;
	  entry.params.mask = 0xffffffff;
	  am1.mode = Dreg; am1.reg = r1;
	  break;
	case 1:
	  entry.execfunc = &INST_ILLG;
	  break;
	case 2:
	  entry.execfunc = &INST_CHK;
	  entry.params.mask = 0xffff;
	  am1.mode = Dreg; am1.reg = r1;
	  break;
	case 3:
	  if (am2.mode == Dreg) {
	    entry.execfunc = &INST_NIMP; // EXTB.L
	  } else {
	    entry.execfunc = &INST_LEA;
	    entry.params.mask = 0xffffffff;
	    am1.mode = Areg; am1.reg = r1;
	  }
	  break;
	}
      } else {
	switch(sz){
	case 0:
	  switch(r1){
	  case 0:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xff;
	    break;
	  case 1:
	    entry.execfunc = &INST_CLR;
	    entry.params.mask = 0xff;
	    break;
	  case 2:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xff;
	    break;
	  case 3:
	    entry.execfunc = &INST_NOT;
	    entry.params.mask = 0xff;
	    break;
	  case 4:
	    if (am2.mode == Areg) {
	      entry.execfunc = &INST_LINK; // this is from 020 upwards
	      entry.params.mask = 0xffffffff;
	      am1.mode = imm;
	    } else {
	      entry.execfunc = &INST_NBCD;
	      entry.params.mask = 0xff;
	    }
	    break;
	  case 5:
	    entry.execfunc = &INST_TST;
	    entry.params.mask = 0xff;
	    break;
	  case 6:
	    entry.execfunc = &INST_NIMP; // MUL[US].L
	    break;
	  case 7:
	    entry.execfunc = &INST_ILLG;
	    break;
	  }
	  break;
	case 1:
	  switch(r1){
	  case 0:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xffff;
	    break;
	  case 1:
	    entry.execfunc = &INST_CLR;
	    entry.params.mask = 0xffff;
	    break;
	  case 2:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xffff;
	    break;
	  case 3:
	    entry.execfunc = &INST_NOT;
	    entry.params.mask = 0xffff;
	    break;
	  case 4:
	    if (am2.mode == Dreg) {
	      entry.execfunc = &INST_SWAP;
	      entry.params.mask = 0xffffffff;
	    } else {
	      entry.execfunc = &INST_LEA; // PEA == LEA ea,-(A7)
	      entry.params.mask = 0xffffffff;
	      am1.mode = Apdi;
	      am1.reg = 7;
	    }
	    break;
	  case 5:
	    entry.execfunc = &INST_TST;
	    entry.params.mask = 0xffff;
	    break;
	  case 6:
	    entry.execfunc = &INST_NIMP; // DIV[US]{L}.L
	    break;
	  case 7:
	    switch(am2.mode){
	    case Areg: case Dreg:
	      entry.execfunc = &INST_TRAP;
	      break;
	    case Aind:
	      entry.execfunc = &INST_LINK;
	      am1.mode = imm;
	      am2.mode = Areg;
	      entry.params.mask = 0xffff;
	      break;
	    case Aipi:
	      entry.execfunc = &INST_UNLK;
	      am2.mode = Areg;
	      break;
	    case Apdi:
	      entry.execfunc = &INST_MOVEAUSP;
	      am2.mode = Areg;
	      break;
	    case Ad16:
	      entry.execfunc = &INST_MOVEUSPA;
	      am2.mode = Areg;
	      break;
	    case Ad8r:
	      am2.mode = imm;
	      switch(am2.reg){
	      case 0: entry.execfunc = &INST_RESET; break;
	      case 1: entry.execfunc = &INST_NOP; break;
	      case 2: entry.execfunc = &INST_STOP; break;
	      case 3: entry.execfunc = &INST_RTE; break;
	      case 4: entry.execfunc = &INST_RTD; break;
	      case 5: entry.execfunc = &INST_RTS; break;
	      case 6: entry.execfunc = &INST_TRAPV; break;
	      case 7: entry.execfunc = &INST_RTR; break;
	      }
	      break;
	    case absw: case absl: case PC16: case PC8r: case imm:
	    case imm3: case ill2: case ill3:
	      entry.execfunc = &INST_ILLG;
	      break;
	    }
	  }
	  break;
	case 2:
	  switch(r1){
	  case 0:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xffffffff;
	    break;
	  case 1:
	    entry.execfunc = &INST_CLR;
	    entry.params.mask = 0xffffffff;
	    break;
	  case 2:
	    entry.execfunc = &INST_NEG;
	    entry.params.mask = 0xffffffff;
	    break;
	  case 3:
	    entry.execfunc = &INST_NOT;
	    entry.params.mask = 0xffffffff;
	    break;
	  case 4:
	    entry.params.mask = 0xffff;
	    if (am2.mode == Dreg) {
	      entry.execfunc = &INST_EXTW;
	    } else {
	      entry.execfunc = &INST_MOVEMRLEA;
	    }
	    break;
	  case 5:
	    entry.execfunc = &INST_TST;
	    entry.params.mask = 0xffffffff;
	    break;
	  case 6:
	    entry.params.mask =0xffff;
	    entry.execfunc = &INST_MOVEMEARL;
	    break;
	  case 7:
	    entry.params.mask =0xffffffff;
	    if (am2.mode == imm) {
	      entry.execfunc = &INST_ILLG;
	    } else {
	      entry.execfunc = &INST_JSR;
	    }
	    break;
	  }
	  break;
	case 3:
	  entry.params.mask = 0xffff;
	  switch(r1){
	  case 0:
	    entry.execfunc = &INST_MOVESREA;
	    break;
	  case 1:
	    entry.execfunc = &INST_MOVECCREA;
	    break;
	  case 2:
	    entry.execfunc = &INST_MOVEEACCR;
	    break;
	  case 3:
	    entry.execfunc = &INST_MOVEEASR;
	    break;
	  case 4:
	    entry.params.mask = 0xffffffff;
	    if (am2.mode == Dreg) {
	      entry.execfunc = &INST_EXTL;
	    } else {
	      entry.execfunc = &INST_MOVEMRLEA;
	    }
	    break;
	  case 5:
	    entry.execfunc = &INST_NIMP; // TAS.B
	    break;
	  case 6:
	    entry.params.mask =0xffffffff;
	    entry.execfunc = &INST_MOVEMEARL;
	    break;
	  case 7:
	    entry.params.mask =0xffffffff;
	    if (am2.mode == imm) {
	      entry.execfunc = &INST_ILLG;
	    } else {
	      entry.execfunc = &INST_JMP;
	    }
	    break;
	  }
	  break;	  
	}
      }
      break; 
    case 0x5000:
      switch(sz){
      case 0: case 1: case 2:
	entry.params.mask = sizemask(sz);
	if (x) {
	  if (am2.mode == Areg) 
	    entry.execfunc = &INST_SUBA/*Q*/;
	  else
	    entry.execfunc = &INST_SUB/*Q*/;

	} else {
	  if (am2.mode == Areg)
	    entry.execfunc = &INST_ADDA/*Q*/;
	  else
	    entry.execfunc = &INST_ADD/*Q*/;
	}
	am1 = am2; am2.mode = imm3; am2.reg = r1 ? r1 : 8;
	break;
      case 3:
	if (am2.mode == Areg) {
	  entry.params.mask = 0xffff; 
	  entry.execfunc = &INST_DBcc;
	  am2.mode = Dreg;
	} else {
	  entry.params.mask = 0xff;
	  entry.execfunc = &INST_Scc;
	}
	am1.mode = imm3;
	am1.reg = (opcode & 0xf00) >> 8;
      }
      break; 
    case 0x6000:
      offset = opcode & 0xff;
      if (x == 1 && r1 == 0) {
	if (offset == 0) {
	  entry.execfunc = &INST_BSRW;
	  entry.params.mask = 0xffff;
	  am2.mode = imm;
	} else {
	  entry.execfunc = &INST_BSRB;
	  entry.params.mask = 0xff;
	  am2.mode = imm3; am2.reg = opcode & 0xff;
	}
      } else {
	if (offset == 0) {
	  entry.execfunc = &INST_BccW;
	  entry.params.mask = 0xffff;
	  am2.mode = imm;
	} else {
	  entry.execfunc = &INST_BccB;
	  entry.params.mask = 0xff;
	  am2.mode = imm3; am2.reg = opcode & 0xff;
	}
      }
      am1.reg = (opcode & 0xf00) >> 8;
      break;
    case 0x7000: 
      if (x) {
	entry.execfunc = &INST_ILLG;
      } else {
	entry.execfunc = &INST_MOVEQ;
	am1.mode = Dreg; am1.reg = r1; entry.params.mask = 0xffffffff;
	am2.mode = imm3; am2.reg = opcode & 0xff;
      }
      break; 
    case 0x8000: 
      if (x) {
	switch(sz) {
	case 0: case 1: case 2:
	  entry.params.mask = sizemask(sz);
	  switch (am2.mode) {
	  case Dreg:
	    if (sz != 0) {
	      entry.execfunc = &INST_NIMP; // PACK, UNPK
	    } else {
	      entry.execfunc = &INST_SBCD;
	      entry.params.mask = 0xff;
	      am1.mode = Dreg;
	      am1.reg = r1;
	    }
	    break;
	  case Areg:
	    if (sz != 0) {
	      entry.execfunc = &INST_NIMP; // PACK, UNPK
	    } else {
	      entry.execfunc = &INST_SBCD;
	      entry.params.mask = 0xff;
	      am1.mode = am2.mode = Apdi;
	      am1.reg = r1;
	    }
	    break;
	  default:
	    entry.execfunc = &INST_OR;
	    am1 = am2; am2.mode = Dreg; am2.reg = r1;
	    break;
	  }
	  break;
	case 3:
	  entry.execfunc = &INST_DIVS;
	  entry.params.mask = 0xffff;
	  am1.mode = Dreg; am1.reg = r1;
	  break;
	}
      } else {
        am1.mode = Dreg; am1.reg = r1;
	switch(sz) {
	case 0: case 1: case 2:
	  entry.execfunc = &INST_OR;
	  entry.params.mask = sizemask(sz);
	  break;
	case 3:
	  entry.execfunc = &INST_DIVU;
	  entry.params.mask = 0xffff;
	  break;
	}
      }
      break; 
    case 0x9000:
      if (sz == 3) {
	entry.execfunc = &INST_SUBA;
	entry.params.mask = x ? 0xffffffff : 0xffff;
	am1.mode = Areg; am1.reg = r1;
      } else {
	entry.execfunc = &INST_SUB;
	entry.params.mask = sizemask(sz);
	if (x) {
	  switch(am2.mode) {
	  case Dreg:
	    entry.execfunc = &INST_SUBX;
	    am1.mode = Dreg;
	    am1.reg = r1;
	    break;
	  case Areg:
	    entry.execfunc = &INST_SUBX;
	    am1.mode = am2.mode = Apdi;
	    am1.reg = r1;
	    break;
	  default:
	    am1 = am2;
	    am2.mode = Dreg;
	    am2.reg = r1;
	    break;
	  }
	} else {
	  am1.mode = Dreg;
	  am1.reg = r1;
	}	
      }
      break; 
    case 0xA000: 
      entry.execfunc = &INST_LINEA;
      break; 
    case 0xB000:
      if (sz == 3) {
	entry.execfunc = &INST_CMPA;
	entry.params.mask = x ? 0xffffffff : 0xffff;
	am1.mode = Areg; am1.reg = r1;
      } else {
	entry.params.mask = sizemask(sz);
	am1.mode = Dreg; am1.reg = r1;
	if (x) {
	  if (am2.mode == Areg){
	    am1.mode = am2.mode = Aipi;
	    am1.reg = r1;
	    entry.execfunc = &INST_CMP/*M*/;
	  } else {
	    entry.execfunc = &INST_EOR;
	    am1 = am2; am2.mode = Dreg; am2.reg = r1;
	  }
	} else {
	  entry.execfunc = &INST_CMP;
	}
      }
      break; 
    case 0xC000: 
      if (x) {
	switch(sz) {
	case 0: case 1: case 2:
	  entry.params.mask = sizemask(sz);
	  switch (am2.mode) {
	  case Dreg:
	    switch(sz) {
	    case 2:
	      entry.execfunc = &INST_ILLG;
	      break;
	    case 1:
	      entry.execfunc = &INST_EXGL;
	      entry.params.mask = 0xffffffff;
	      am1 = am2;
	      am2.mode = Dreg; am2.reg = r1;
	      break;
	    case 0:
	      entry.execfunc = &INST_ABCD;
	      entry.params.mask = 0xff;
	      am1.mode = Dreg; am1.reg = r1;
	      break;
	    }
	    break;
	  case Areg:
	    switch(sz) {
	    case 2:
	      entry.execfunc = &INST_EXGL;
	      entry.params.mask = 0xffffffff;
	      am1 = am2;
	      am2.mode = Dreg; am2.reg = r1;
	      break;
	    case 1:
	      entry.execfunc = &INST_EXGL;
	      entry.params.mask = 0xffffffff;
	      am1 = am2;
	      am2.mode = Areg; am2.reg = r1;
	      break;
	    case 0:
	      entry.execfunc = &INST_ABCD;
	      entry.params.mask = 0xff;
	      am1.mode = am2.mode = Apdi;
	      am1.reg = r1;
	      break;
	    }
	    break;
	  default:
	    entry.execfunc = &INST_AND;
	    am1 = am2;
	    am2.mode = Dreg; am2.reg = r1;
	    break;
	  }
	  break;
	case 3:
	  entry.execfunc = &INST_MULS;
	  entry.params.mask = 0xffff;
	  am1.mode = Dreg; am1.reg = r1;
	  break;
	}
      } else {
        am1.mode = Dreg; am1.reg = r1;
	switch(sz) {
	case 0: case 1: case 2:
	  entry.execfunc = &INST_AND;
	  entry.params.mask = sizemask(sz);
	  break;
	case 3:
	  entry.execfunc = &INST_MULU;
	  entry.params.mask = 0xffff;
	  break;
	}
      }
      break; 
    case 0xD000: 
      if (sz == 3) {
	entry.execfunc = &INST_ADDA;
	entry.params.mask = x ? 0xffffffff : 0xffff;
	am1.mode = Areg; am1.reg = r1;
      } else {
	entry.execfunc = &INST_ADD;
	entry.params.mask = sizemask(sz);
	if (x) {
	  switch(am2.mode) {
	  case Dreg:
	    entry.execfunc = &INST_ADDX;
	    am1.mode = Dreg;
	    am1.reg = r1;
	    break;
	  case Areg:
	    entry.execfunc = &INST_ADDX;
	    am1.mode = am2.mode = Apdi;
	    am1.reg = r1;
	    break;
	  default:
	    am1 = am2;
	    am2.mode = Dreg;
	    am2.reg = r1;
	    break;
	  }
	} else {
	  am1.mode = Dreg;
	  am1.reg = r1;
	}
      }
      break;      
    case 0xE000: 
      int type;
     
      if (sz == 3) {
	entry.params.mask = 0xffff;
	type = r1*2 + x;
	switch(type) {
	case 0: entry.execfunc = &INST_ASR1; break;
	case 1: entry.execfunc = &INST_ASL1; break;
	case 2: entry.execfunc = &INST_LSR1; break;
	case 3: entry.execfunc = &INST_LSL1; break;
	case 4: entry.execfunc = &INST_ROXR1; break;
	case 5: entry.execfunc = &INST_ROXL1; break;
	case 6: entry.execfunc = &INST_ROR1; break;
	case 7: entry.execfunc = &INST_ROL1; break;
	default:
	  entry.execfunc = &INST_NIMP; // 020 bit fiends
	  break;
	}
	if (type < 8) {
	  if (am2.mode == Dreg || am2.mode == Areg || am2.mode == PC16 ||
	      am2.mode == PC8r || am2.mode == imm) {
	    entry.execfunc = &INST_ILLG;
	  }
	}
	am1 = am2;
      } else {
	type = x + (m2 & 3)*2;
	entry.params.mask = sizemask(sz);
	switch(type) {
	case 0: entry.execfunc = &INST_ASR; break;
	case 1: entry.execfunc = &INST_ASL; break;
	case 2: entry.execfunc = &INST_LSR; break;
	case 3: entry.execfunc = &INST_LSL; break;
	case 4: entry.execfunc = &INST_ROXR; break;
	case 5: entry.execfunc = &INST_ROXL; break;
	case 6: entry.execfunc = &INST_ROR; break;
	case 7: entry.execfunc = &INST_ROL; break;
	}
	am1.mode = Dreg; am1.reg = r2;
	if (m2 > 3) {
	  am2.mode = Dreg; am2.reg = r1;
	} else {
	  am2.mode = imm3;
	  am2.reg = r1 ? r1 : 8;
	}
      }
      break;
    case 0xF000: 
      entry.execfunc = &INST_LINEF;
      break; 
    }
    entry.params.dest = am1; entry.params.src = am2;
    instr_dectab[opcode] = entry;
  }
// Mahlzeit!
}
