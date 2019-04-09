 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * custom chip support v0.0
  * v0.0: 950228
  *
  * (c) 1995 Bernd Schmidt
  */

void clockcycle();
void customreset();
int intlev();
void dumpcustom();

enum ints {
  i_tbe, i_dskblk, i_soft, i_ports, i_coper, i_vertb, 
  i_aud0, i_aud1, i_aud2, i_aud3, i_rbf, i_dsksyn, i_exter, i_master
};
