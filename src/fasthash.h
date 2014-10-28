#pragma once

extern "C" bool scanhash_X_avxaes(uint32_t *pdata, const uint32_t *ptarget);
extern "C" bool scanhash_X_noavxaes(uint32_t *pdata, const uint32_t *ptarget);
extern "C" void init_Xhash_contexts_avxaes();
extern "C" void init_Xhash_contexts_noavxaes();

inline bool CPUsupportsAVXandAES()
{
    printf("cpuid, eax = 1.\n");

    int eax, ebx, ecx, edx;
    __asm__ __volatile__ ("cpuid": "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" ((int)1));
    int Mask =
            (1 << 19) | // SSE4.1
            (1 << 25) | // AES
            (1 << 27) | // AVX OS support
            (1 << 28) ; // AVX

    printf("eax = %u, ebx = %u, ecx = %u, edx = %u.\n", eax, ebx, ecx, edx);

    return (ecx & Mask) == Mask;
}

static bool g_CPUsupportsAVXandAES;

inline void init_Xhash_contexts()
{
    g_CPUsupportsAVXandAES = CPUsupportsAVXandAES();

    printf("CPU supports AVX and AES = %s\n", g_CPUsupportsAVXandAES? "true" : "false");

    if (g_CPUsupportsAVXandAES)
        init_Xhash_contexts_avxaes();
    else
        init_Xhash_contexts_noavxaes();
}

inline bool scanhash_X(uint32_t *pdata, const uint32_t *ptarget)
{
    if (g_CPUsupportsAVXandAES)
        return scanhash_X_avxaes(pdata, ptarget);
    else
        return scanhash_X_noavxaes(pdata, ptarget);
}
