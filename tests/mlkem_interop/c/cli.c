#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
static void hex(const unsigned char *b, size_t n){ for(size_t i=0;i<n;i++) printf("%02x", b[i]); printf("\n"); }
static size_t unhex(const char *s, unsigned char *o){ size_t n=strlen(s)/2; for(size_t i=0;i<n;i++){ unsigned v; sscanf(s+2*i,"%2x",&v); o[i]=(unsigned char)v;} return n; }
int main(int argc, char **argv){
  unsigned char pk[pqcrystals_kyber768_PUBLICKEYBYTES], sk[pqcrystals_kyber768_SECRETKEYBYTES], ct[pqcrystals_kyber768_CIPHERTEXTBYTES], ss[pqcrystals_kyber768_BYTES];
  if(argc==2 && !strcmp(argv[1],"keygen")){ pqcrystals_kyber768_ref_keypair(pk,sk); hex(pk,sizeof pk); hex(sk,sizeof sk); return 0; }
  if(argc==3 && !strcmp(argv[1],"encaps")){ unhex(argv[2],pk); pqcrystals_kyber768_ref_enc(ct,ss,pk); hex(ct,sizeof ct); hex(ss,sizeof ss); return 0; }
  if(argc==4 && !strcmp(argv[1],"decaps")){ unhex(argv[2],sk); unhex(argv[3],ct); pqcrystals_kyber768_ref_dec(ss,ct,sk); hex(ss,sizeof ss); return 0; }
  return 2; }
