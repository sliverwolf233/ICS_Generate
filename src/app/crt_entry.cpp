// crt_entry.cpp - CRT-free process entry point and compiler runtime shims.
//
// The image is linked with /NODEFAULTLIB (MSVC) or -nostdlib -nostartfiles
// -nodefaultlibs (MinGW), so no C runtime exists: Windows calls AppEntry
// directly (the build passes /ENTRY:AppEntry or -Wl,-e,AppEntry) and every
// helper the compiler may emit a call to has to be defined here. Keep this file
// small - it is the only place allowed to define these symbols.
#include <windows.h>

extern "C" int AppMain(void);

// ---------------------------------------------------------------- entry -----
// Raw process entry point. x64 uses no name decoration, and the x86 linker
// resolves /ENTRY:AppEntry against the __stdcall name automatically.
extern "C" unsigned long WINAPI AppEntry(void) {
    int rc = AppMain();
    ExitProcess((UINT)rc);
    return 0;   // ExitProcess never returns; keeps MSVC /W4 and GCC quiet.
}

// ------------------------------------------------------- memory routines ----
// MSVC would otherwise replace the loops below with intrinsic calls to the very
// functions they implement, so the intrinsic substitution is turned off.
#if defined(_MSC_VER)
#pragma function(memset, memcpy, memmove, memcmp)
#endif
// GCC rewrites byte loops into memset/memcpy calls from -O2 on; that would make
// these definitions recurse into themselves.
#if defined(__GNUC__)
#define ICSG_NO_PATTERN __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define ICSG_NO_PATTERN
#endif

extern "C" ICSG_NO_PATTERN void* __cdecl memset(void* dst, int value, size_t count) {
    unsigned char* d = (unsigned char*)dst;
    while (count > 0) { *d++ = (unsigned char)value; --count; }
    return dst;
}

extern "C" ICSG_NO_PATTERN void* __cdecl memcpy(void* dst, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (count > 0) { *d++ = *s++; --count; }
    return dst;
}

extern "C" ICSG_NO_PATTERN void* __cdecl memmove(void* dst, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || count == 0) return dst;
    if (d < s) {
        while (count > 0) { *d++ = *s++; --count; }
    } else {
        d += count;
        s += count;
        while (count > 0) { *--d = *--s; --count; }
    }
    return dst;
}

extern "C" ICSG_NO_PATTERN int __cdecl memcmp(const void* a, const void* b, size_t count) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    while (count > 0) {
        if (*x != *y) return (int)*x - (int)*y;
        ++x;
        ++y;
        --count;
    }
    return 0;
}

// MSVC references _fltused from every object that initialises floating point
// data; the symbol keeps the linker from pulling in the (absent) CRT. The
// language-linkage block (instead of the extern keyword) keeps GCC warning-clean.
extern "C" {
int _fltused = 0;
}
// ------------------------------------------------ x86 64-bit math ----
// MSVC x86 has no native 64-bit divide; /NODEFAULTLIB links need these CRT
// helper routines. Compiled only for 32-bit MSVC (x64 and MinGW use native
// instructions / libgcc), algorithms verified against native operators
// with 1,000,000 random groups on x64 (see build-ui/_int64_test.cpp).
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" {
// ---- 64-bit helper routines for x86 (MSVC has no native 64/64 divide) ----
// Implemented with 32-bit-friendly operations only (constant shifts, so the
// compiler never emits calls to the shift helpers) and no recursion, so the
// set below is closed: no further CRT symbols are pulled in.
static unsigned long long x86_shl64(unsigned long long v, int n) {
    while (n >= 16) { v <<= 16; n -= 16; }
    while (n > 0)   { v <<= 1;  --n; }
    return v;
}

static unsigned long long x86_shr64(unsigned long long v, int n) {
    while (n >= 16) { v >>= 16; n -= 16; }
    while (n > 0)   { v >>= 1;  --n; }
    return v;
}

// Restoring division, one bit per step, MSB first. Requires den < 2^63 so
// that (r * 2 + next bit) can never overflow; larger divisors are handled
// explicitly (quotient is 0 or 1). q/r through pointers so __allrem shares
// one implementation with __alldiv.
static void x86_udivmod(unsigned long long num, unsigned long long den,
                        unsigned long long* q, unsigned long long* r) {
    if (den == 0) { *q = 0; *r = 0; return; }   // defensive: CRT would raise
    if (num < den) { *q = 0; *r = num; return; }
    if ((den >> 63) != 0) { *q = 1; *r = num - den; return; }
    unsigned long long qq = 0;
    unsigned long long rr = 0;
    unsigned long long bit = x86_shl64(1ULL, 63);
    for (int i = 0; i < 64; ++i) {
        rr = x86_shl64(rr, 1);
        if (num & bit) rr |= 1;
        bit = x86_shr64(bit, 1);
        qq = x86_shl64(qq, 1);
        if (rr >= den) { rr -= den; qq |= 1; }
    }
    *q = qq;
    *r = rr;
}


struct IcsDvrmS { long long quot; long long rem; };
struct IcsDvrmU { unsigned long long quot; unsigned long long rem; };




// 64x64 -> 64 multiply from four 32x32 -> 64 products (mod 2^64 wraps exactly
// like the native operation, so signed inputs need no special case).
__declspec(noinline) __declspec(noinline) long long __cdecl __allmul(long long a, long long b) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned int alo = (unsigned int)ua, ahi = (unsigned int)(ua >> 32);
    unsigned int blo = (unsigned int)ub, bhi = (unsigned int)(ub >> 32);
    unsigned long long lo    = (unsigned long long)alo * blo;
    unsigned long long cross = (unsigned long long)alo * bhi
                             + (unsigned long long)ahi * blo;
    return (long long)(lo + x86_shl64(cross, 32));
}

__declspec(noinline) __declspec(noinline) long long __cdecl __alldiv(long long a, long long b) {
    unsigned long long ua = a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b;
    unsigned long long q, r;
    x86_udivmod(ua, ub, &q, &r);
    if ((a < 0) != (b < 0)) q = 0 - q;
    return (long long)q;
}

__declspec(noinline) __declspec(noinline) long long __cdecl __allrem(long long a, long long b) {
    unsigned long long ua = a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b;
    unsigned long long q, r;
    x86_udivmod(ua, ub, &q, &r);
    if (a < 0) r = 0 - r;   // C truncating division: remainder takes a sign
    return (long long)r;
}

// MSVC x86 ABI: a struct larger than 8 bytes is returned through a hidden
// sret pointer, which is exactly what the CRT caller of __alldvrm expects
// (quot at [ptr], rem at [ptr+8]); returning the struct makes MSVC emit that.
__declspec(noinline) __declspec(noinline) IcsDvrmS __cdecl __alldvrm(long long a, long long b) {
    IcsDvrmS out;
    unsigned long long ua = a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b;
    unsigned long long q, r;
    x86_udivmod(ua, ub, &q, &r);
    if ((a < 0) != (b < 0)) q = 0 - q;
    if (a < 0) r = 0 - r;
    out.quot = (long long)q;
    out.rem  = (long long)r;
    return out;
}

__declspec(noinline) unsigned __declspec(noinline) long long __cdecl __aulldiv(unsigned long long a,
                                                          unsigned long long b) {
    unsigned long long q, r;
    x86_udivmod(a, b, &q, &r);
    return q;
}

__declspec(noinline) unsigned __declspec(noinline) long long __cdecl __aullrem(unsigned long long a,
                                                          unsigned long long b) {
    unsigned long long q, r;
    x86_udivmod(a, b, &q, &r);
    return r;
}

__declspec(noinline) __declspec(noinline) IcsDvrmU __cdecl __aulldvrm(unsigned long long a,
                                                 unsigned long long b) {
    IcsDvrmU out;
    x86_udivmod(a, b, &out.quot, &out.rem);
    return out;
}




}  // extern "C"
#endif  // _MSC_VER && _M_IX86

