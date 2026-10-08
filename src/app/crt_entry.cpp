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
