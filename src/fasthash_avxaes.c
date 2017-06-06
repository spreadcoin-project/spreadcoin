#pragma GCC target ("sse2,sse3,sse4.1,avx,aes")

#define AES_NI
#define AES_NI_GR
#define INITHASH_NAME init_Xhash_contexts_avxaes
#define SCANHASH_NAME scanhash_X_avxaes


#include "fasthash_impl.c"
