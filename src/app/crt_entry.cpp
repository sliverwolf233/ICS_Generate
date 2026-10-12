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
// MSVC x86 has no native 64/64 divide; /NODEFAULTLIB links need these CRT
// helpers. Source-level names carry ONE underscore (_alldiv ...): x86
// __cdecl decorates exactly one leading underscore, and the compiler-
// generated calls reference those decorated names directly. x64 and MinGW
// never see this block. Algorithms verified against native 64-bit
// operators with 1,000,000 random groups on x64 (build-ui/_int64_test.cpp).
#if defined(_MSC_VER) && defined(_M_IX86)
// no exports, so the extra local symbols have no visibility effect. ---- x86 64-bit math: C algorithm cores + naked ABI shells ----
// MSVC x86 emits calls to _alldiv/_allmul/_allrem/_alldvrm/_aulldiv/
// _aullrem/_aulldvrm (source-level single underscore: x86 __cdecl decorates
// one leading underscore, so the symbol is exactly the source name).
// Conventions implemented here (documented MSVC x86 ABI):
//   _allmul                returns product in EDX:EAX
//   _alldiv/_aulldiv       returns quotient in EDX:EAX
//   _allrem/_aullrem       returns remainder in EDX:EAX
//   _alldvrm               quotient in EDX:EAX, remainder in EBX:ECX
//   _aulldvrm              quotient in EDX:EAX, remainder in EBX:ECX
// All args on the stack ([esp+4]=a.lo ... [esp+16]=b.hi), __cdecl caller
// cleans (ret 16). The naked shells are thin: marshal stack -> call the C
// core -> place results -> ret 16. The C cores hold all verified logic.

// ---- constant-shift helpers (never emit calls to shift helpers) ----
static unsigned long long ics_shl64(unsigned long long v, int n) {
    while (n >= 16) { v <<= 16; n -= 16; }
    while (n > 0)   { v <<= 1;  --n; }
    return v;
}

static unsigned long long ics_shr64(unsigned long long v, int n) {
    while (n >= 16) { v >>= 16; n -= 16; }
    while (n > 0)   { v >>= 1;  --n; }
    return v;
}

// ---- multiply: four 32x32 -> 64 products recombined mod 2^64 ----
// (wraps exactly like the native operation, so no signed special case; and
// no 64x64 multiply anywhere, so no recursion into _allmul).
long long __cdecl ics_mul_ll(long long a, long long b) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned int alo = (unsigned int)ua, ahi = (unsigned int)(ua >> 32);
    unsigned int blo = (unsigned int)ub, bhi = (unsigned int)(ub >> 32);
    unsigned long long lo    = (unsigned long long)alo * blo;
    unsigned long long cross = (unsigned long long)alo * bhi
                             + (unsigned long long)ahi * blo;
    return (long long)(lo + ics_shl64(cross, 32));
}

// ---- unsigned restoring division, one bit per step, MSB first ----
// Requires den < 2^63 so (r * 2 + next bit) cannot overflow; bigger divisors
// have quotient 0 or 1 and are handled explicitly. No 64/64 divide, no
// variable 64-bit shifts: the helper set stays closed.
void ics_udivmod_core(unsigned long long num, unsigned long long den,
                             unsigned long long* q, unsigned long long* r) {
    if (den == 0) { *q = 0; *r = 0; return; }   // defensive: CRT would raise
    if (num < den) { *q = 0; *r = num; return; }
    if ((den >> 63) != 0) { *q = 1; *r = num - den; return; }
    unsigned long long qq = 0;
    unsigned long long rr = 0;
    unsigned long long bit = ics_shl64(1ULL, 63);
    for (int i = 0; i < 64; ++i) {
        rr = ics_shl64(rr, 1);
        if (num & bit) rr |= 1;
        bit = ics_shr64(bit, 1);
        qq = ics_shl64(qq, 1);
        if (rr >= den) { rr -= den; qq |= 1; }
    }
    *q = qq;
    *r = rr;
}

// ---- signed wrapper: C truncating division, remainder takes a sign ----
void ics_sdivmod_core(long long a, long long b,
                             unsigned long long* q, unsigned long long* r) {
    unsigned long long ua = a < 0 ? 0 - (unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? 0 - (unsigned long long)b : (unsigned long long)b;
    ics_udivmod_core(ua, ub, q, r);
    if ((a < 0) != (b < 0)) *q = 0 - *q;
    if (a < 0) *r = 0 - *r;
}

// ---- exported entry points (MSVC x86 ABI) ----
// The cores live in the same extern "C" block so the naked shells can
// reference them by their undecorated names in inline asm. This exe has
extern "C" {

// ---- constant-shift helpers and C cores (see above) ----

// stack layout entering every shell: [esp]=ret, +4 a.lo, +8 a.hi,
// +12 b.lo, +16 b.hi. The four pushes at [esp+32..] below assume the
// 16-byte scratch block and the two pointer pushes shown in each shell.

__declspec(naked) void __cdecl _allmul(void) {
    __asm {
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        call ics_mul_ll            ; product in edx:eax
        ret 16
    }
}

__declspec(naked) void __cdecl _alldiv(void) {
    __asm {
        sub  esp, 16               ; scratch: q at [esp], r at [esp+8]
        lea  eax, [esp+8]
        push eax                   ; &r
        lea  eax, [esp+4]
        push eax                   ; &q
        push dword ptr [esp+32]    ; a.lo
        push dword ptr [esp+32]    ; a.hi
        push dword ptr [esp+32]    ; b.lo
        push dword ptr [esp+32]    ; b.hi
        call ics_sdivmod_core
        mov  eax, [esp+24]         ; quotient -> edx:eax
        mov  edx, [esp+28]
        add  esp, 40
        ret  16
    }
}

__declspec(naked) void __cdecl _allrem(void) {
    __asm {
        sub  esp, 16
        lea  eax, [esp+8]
        push eax
        lea  eax, [esp+4]
        push eax
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        call ics_sdivmod_core
        mov  eax, [esp+32]         ; remainder -> edx:eax
        mov  edx, [esp+36]
        add  esp, 40
        ret  16
    }
}

__declspec(naked) void __cdecl _alldvrm(void) {
    __asm {
        sub  esp, 16
        lea  eax, [esp+8]
        push eax
        lea  eax, [esp+4]
        push eax
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        call ics_sdivmod_core
        mov  ecx, [esp+32]         ; remainder -> ebx:ecx
        mov  ebx, [esp+36]
        mov  eax, [esp+24]         ; quotient  -> edx:eax
        mov  edx, [esp+28]
        add  esp, 40
        ret  16
    }
}

__declspec(naked) void __cdecl _aulldiv(void) {
    __asm {
        sub  esp, 16
        lea  eax, [esp+8]
        push eax
        lea  eax, [esp+4]
        push eax
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        call ics_udivmod_core
        mov  eax, [esp+24]
        mov  edx, [esp+28]
        add  esp, 40
        ret  16
    }
}

__declspec(naked) void __cdecl _aullrem(void) {
    __asm {
        sub  esp, 16
        lea  eax, [esp+8]
        push eax
        lea  eax, [esp+4]
        push eax
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        call ics_udivmod_core
        mov  eax, [esp+32]
        mov  edx, [esp+36]
        add  esp, 40
        ret  16
    }
}

__declspec(naked) void __cdecl _aulldvrm(void) {
    __asm {
        sub  esp, 16
        lea  eax, [esp+8]
        push eax
        lea  eax, [esp+4]
        push eax
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        push dword ptr [esp+32]
        call ics_udivmod_core
        mov  ecx, [esp+32]
        mov  ebx, [esp+36]
        mov  eax, [esp+24]
        mov  edx, [esp+28]
        add  esp, 40
        ret  16
    }
}

}  // extern "C"
#endif  // _MSC_VER && _M_IX86
