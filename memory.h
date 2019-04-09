 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * memory management v0.1
  * 
  * (c) 1995 Bernd Schmidt
  */

const int chipmem_size = 512;
const int fastmem_size = 0;
const int bogomem_size = 0; // C00000 crap mem

class address64k {
 public:
  virtual UWORD wget(UWORD addr) = 0;
  virtual void wput(UWORD addr, UWORD value) = 0;
  virtual UBYTE bget(UWORD addr) = 0;
  virtual void bput(UWORD addr, UBYTE value) = 0;
};

class ram64k: public address64k {
 protected:
  UWORD ram[32768];
 public:
  virtual UWORD wget(UWORD addr);
  virtual void wput(UWORD addr, UWORD value);
  virtual UBYTE bget(UWORD addr);
  virtual void bput(UWORD addr, UBYTE value);
};

class custom64k: public address64k {
  struct cchipaddr {
    UWORD (*get)();
    void (*put)(UWORD);
  };
  static cchipaddr *cchiptable;
  struct custom_func {
    UWORD addr;
    cchipaddr fptrs;
  };
  static custom_func functable[]; 

public:
  custom64k();
  virtual UWORD wget(UWORD addr);
  virtual void wput(UWORD addr, UWORD value);
  virtual UBYTE bget(UWORD addr);
  virtual void bput(UWORD addr, UBYTE value);
};

class cia64k: public address64k {
public:
  virtual UWORD wget(UWORD addr);
  virtual void wput(UWORD addr, UWORD value);
  virtual UBYTE bget(UWORD addr);
  virtual void bput(UWORD addr, UBYTE value);
};

class amigamemory {
  private:
    address64k *banks[256]; // memory size is restricted to 16M
  public:
    amigamemory();
    ~amigamemory();
    
    UWORD get_word(CPTR addr) { return banks[addr>>16]->wget(addr & 0xffff); }
    UBYTE get_byte(CPTR addr) { return banks[addr>>16]->bget(addr & 0xffff); }
    void put_word(CPTR a,UWORD w) { banks[a>>16]->wput(a & 0xffff, w); }
    void put_byte(CPTR a,UBYTE b) { banks[a>>16]->bput(a & 0xffff, b); }
};

extern amigamemory mem;
