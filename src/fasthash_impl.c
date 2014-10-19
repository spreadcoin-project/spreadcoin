#include <stdbool.h>
#include <string.h>
#include <stdint.h>

//--
#include "x5/luffa_for_sse2.h" //sse2 opt
//----
#include "x5/cubehash_sse2.h" //sse2 opt
//--------------------------
#include "x5/sph_shavite.h"
//-----simd vect128---------
#include "x5/vect128/nist.h"
//-----------

//#define AES_NI
//#define AES_NI_GR

#ifdef AES_NI
#include "x5/echo512/ccalik/aesni/hash_api.h"
#else
#include "x5/sph_echo.h"
#endif


//----
//#include "blake.c"
#include "sph_blake.h"
//#include "x5/blake/sse41/hash.c"
#include "x6/bmw.c"
#include "x6/keccak.c"
#include "x6/skein.c"
#include "x6/jh_sse2_opt64.h"
//#include "groestl.c"
#ifdef AES_NI_GR
#include "x6/groestl/aesni/hash-groestl.h"
#else
#if 1
#include "x6/grso.c"
#ifndef PROFILERUN
#include "x6/grso-asm.c"
#endif
#else
#include "x6/grss_api.h"
#endif
#endif  //AES-NI_GR


/*define data alignment for different C compilers*/
#if defined(__GNUC__)
#define DATA_ALIGNXY(x,y) x __attribute__ ((aligned(y)))
#else
#define DATA_ALIGNXY(x,y) __declspec(align(y)) x
#endif

#ifdef AES_NI
#ifdef AES_NI_GR
typedef struct {
	sph_shavite512_context  shavite1;
	hashState_echo		echo1;
	hashState_groestl groestl;
	hashState_luffa luffa;
	cubehashParam cubehash;
	hashState_sd ctx_simd1;
    sph_blake512_context blake1;
} Xhash_context_holder;
#else
typedef struct{
	sph_shavite512_context shavite1;
	hashState_echo	echo1;
	hashState_luffa	luffa;
	cubehashParam cubehash;
	hashState_sd ctx_simd1;
    sph_blake512_context blake1;
} Xhash_context_holder;
#endif
#else
typedef struct {
	sph_shavite512_context  shavite1;
	sph_echo512_context		echo1;
	hashState_luffa	luffa;
	cubehashParam	cubehash;
	hashState_sd ctx_simd1;
    grsoState groestl;
    sph_blake512_context blake1;
} Xhash_context_holder;
#endif

Xhash_context_holder base_contexts;

void INITHASH_NAME(){

	//---luffa---
	init_luffa(&base_contexts.luffa,512);
	//--ch sse2---
	cubehashInit(&base_contexts.cubehash,512,16,32);
	//-------
	sph_shavite512_init(&base_contexts.shavite1);
	//---echo sphlib or AESNI-----------
	#ifdef AES_NI
  	init_echo(&base_contexts.echo1, 512);
	#else
	sph_echo512_init(&base_contexts.echo1);
	#endif
	//---local simd var ---
	init_sd(&base_contexts.ctx_simd1,512);
	
    sph_blake512_init(&base_contexts.blake1);
}

static inline void Xhash(void *state, const void *input)
{
	Xhash_context_holder ctx;

//	uint32_t hashA[16], hashB[16];


	memcpy(&ctx, &base_contexts, sizeof(base_contexts));
	#ifdef AES_NI_GR
	init_groestl(&ctx.groestl);
	#endif

	DATA_ALIGNXY(unsigned char hashbuf[128],16);
	size_t hashptr;
	DATA_ALIGNXY(sph_u64 hashctA,8);
	DATA_ALIGNXY(sph_u64 hashctB,8);

	#ifndef AES_NI_GR
	grsoState sts_grs;
	#endif


	DATA_ALIGNXY(unsigned char hash[128],16);
	/* proably not needed */
	memset(hash, 0, 128);
	//blake1-bmw2-grs3-skein4-jh5-keccak6-luffa7-cubehash8-shavite9-simd10-echo11
	//---blake1---
    sph_blake512(&ctx.blake1, input, 185);
    sph_blake512_close(&ctx.blake1, hash);

	//---bmw2---
	DECL_BMW;
	BMW_I;
	BMW_U;
	#define M(x)    sph_dec64le_aligned(data + 8 * (x))
	#define H(x)    (h[x])
	#define dH(x)   (dh[x])
        BMW_C;
	#undef M
	#undef H
	#undef dH
	//---grs3----

	#ifdef AES_NI_GR
	update_groestl(&ctx.groestl, (char*)hash,512);
	final_groestl(&ctx.groestl, (char*)hash);
	#else
	GRS_I;
	GRS_U;
	GRS_C;
	#endif
	//---skein4---
	DECL_SKN;
	SKN_I;
	SKN_U;
	SKN_C;
	//---jh5------
	DECL_JH;
	JH_H;
	//---keccak6---
	DECL_KEC;
	KEC_I;
	KEC_U;
	KEC_C;

//	asm volatile ("emms");
	//--- luffa7
	update_luffa(&ctx.luffa,(const BitSequence*)hash,512);
	final_luffa(&ctx.luffa,(BitSequence*)hash+64);
	//---cubehash---
	cubehashUpdate(&ctx.cubehash,(const byte*) hash+64,64);
	cubehashDigest(&ctx.cubehash,(byte*)hash);
	//---shavite---
	sph_shavite512 (&ctx.shavite1, hash, 64);
	sph_shavite512_close(&ctx.shavite1, hash+64);
	//sph_simd512 (&ctx.simd1, hashA, 64);
	// sph_simd512_close(&ctx.simd1, hashB);
	//-------simd512 vect128 --------------
	update_sd(&ctx.ctx_simd1,(const BitSequence *)hash+64,512);
	final_sd(&ctx.ctx_simd1,(BitSequence *)hash);
	//---echo---
	#ifdef AES_NI
	update_echo (&ctx.echo1,(const BitSequence *) hash, 512);
	final_echo(&ctx.echo1, (BitSequence *) hash+64);
	#else
	sph_echo512 (&ctx.echo1, hash, 64);
	sph_echo512_close(&ctx.echo1, hash+64);
	#endif

	memcpy(state, hash+64, 32);
}

static inline bool fulltest(const uint32_t *hash, const uint32_t *target)
{
    int i;
    bool rc = true;

    for (i = 7; i >= 0; i--) {
        if (hash[i] > target[i]) {
            rc = false;
            break;
        }
        if (hash[i] < target[i]) {
            rc = true;
            break;
        }
    }

    return rc;
}

bool SCANHASH_NAME(uint32_t *pdata, const uint32_t *ptarget)
{
    uint32_t i = pdata[21];
    const uint32_t max_nonce = pdata[21] + 64;

    uint32_t hash64[8] __attribute__((aligned(32)));

    if (ptarget[7]==0)
    {
        do
        {
            pdata[21] = i++;
            Xhash(hash64, pdata);
            if (((hash64[7]&0xFFFFFFFF)==0) && fulltest(hash64, ptarget))
                return true;
        } while(i < max_nonce);
    }
    else if (ptarget[7]<=0xF)
    {
        do
        {
             pdata[21] = i++;
             Xhash(hash64, pdata);
             if (((hash64[7]&0xFFFFFFF0)==0) && fulltest(hash64, ptarget))
                 return true;
        } while(i < max_nonce);
    }
    else if (ptarget[7]<=0xFF)
    {
        do
        {
            pdata[21] = i++;
            Xhash(hash64, pdata);
            if (((hash64[7]&0xFFFFFF00)==0) && fulltest(hash64, ptarget))
                 return true;
        } while(i < max_nonce);
    }
    else if (ptarget[7]<=0xFFF)
    {
        do
        {
            pdata[21] = i++;
            Xhash(hash64, pdata);
            if (((hash64[7]&0xFFFFF000)==0) && fulltest(hash64, ptarget))
                return true;
        } while(i < max_nonce);
    }
    else if (ptarget[7]<=0xFFFF)
    {
        do
        {
            pdata[21] = i++;
            Xhash(hash64, pdata);
            if (((hash64[7]&0xFFFF0000)==0) && fulltest(hash64, ptarget))
                return true;
        } while(i < max_nonce);
    }
    else
    {
        do
        {
            pdata[21] = i++;
            Xhash(hash64, pdata);
            if (fulltest(hash64, ptarget))
                return true;
        } while(i < max_nonce);
    }

    return false;
}
