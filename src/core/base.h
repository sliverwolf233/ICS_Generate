// base.h - CRT-free runtime: memory, UTF-16 strings, UTF-8 codec, file I/O.
// FROZEN CONTRACT (owner: Lead). Do not change signatures; request changes from the Lead.
#pragma once
#ifndef ICSG_BASE_H
#define ICSG_BASE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define ICSG_ARRAY_COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

// ---------------------------------------------------------------- memory ----
// Backed by the Win32 process heap. No CRT. Never returns NULL for n>0
// (fails fast by terminating the process when the heap is exhausted).
void* xmalloc(size_t bytes);
void* xrealloc(void* block, size_t bytes);
void  xfree(void* block);
void  xmemzero(void* dst, size_t bytes);
void  xmemcopy(void* dst, const void* src, size_t bytes);
void  xmemmove(void* dst, const void* src, size_t bytes);
bool  xmemeq(const void* a, const void* b, size_t bytes);

// ------------------------------------------------------------------ WStr ----
// Owned, NUL-terminated UTF-16 string. Invariants for an initialised WStr:
//   data == NULL  <=>  cap == 0 && len == 0
//   data != NULL  =>   cap >= 1, len >= 0, len < cap, data[len] == 0
struct WStr {
    wchar_t* data;
    int      len;
    int      cap;
};

void wstr_init(WStr* s);
void wstr_free(WStr* s);
void wstr_clear(WStr* s);
// Grows the buffer so that at least 'needCap' wchar_t (terminator included) fit.
// Returns false only on allocation failure.
bool wstr_reserve(WStr* s, int needCap);
void wstr_set(WStr* s, const wchar_t* src);
void wstr_set_n(WStr* s, const wchar_t* src, int count);
void wstr_append(WStr* s, const wchar_t* src);
void wstr_append_n(WStr* s, const wchar_t* src, int count);
void wstr_append_ch(WStr* s, wchar_t ch);
// Decimal, optional minimum digit count (zero padded): 7, 2 -> "07".
void wstr_append_num(WStr* s, long long value, int minDigits);
bool wstr_is_empty(const WStr* s);
bool wstr_equals(const WStr* s, const wchar_t* rhs);
bool wstr_equals_ci(const WStr* s, const wchar_t* rhs);
// Case-insensitive ASCII prefix test against a length-delimited buffer.
bool wstr_starts_ci(const wchar_t* text, int len, const wchar_t* prefix);
// Never NULL: returns L"" for an empty/uninitialised WStr.
const wchar_t* wstr_c(const WStr* s);

// ------------------------------------------------------------ UTF-8 codec ----
// Returns the number of bytes a UTF-16 buffer encodes to (no terminator), or -1
// when an unpaired surrogate is found.
int utf8_encoded_len(const wchar_t* src, int srcLen);
// Writes at most outCap bytes to out (no terminator); returns bytes written, or
// -1 when outCap is too small or the input has an unpaired surrogate.
int utf8_encode(const wchar_t* src, int srcLen, char* out, int outCap);
// Decodes at most outCap wchar_t; returns the number of wchar_t written, or -1
// when outCap is too small or the input is malformed (invalid sequence, overlong
// form, surrogate range, > U+10FFFF). Invalid sequences are never silently kept.
int utf8_decode(const char* src, int srcLen, wchar_t* out, int outCap);
// Decodes one code point starting at *pos; advances *pos. Returns 0xFFFD and
// advances by one byte for malformed input.
wchar_t utf8_next(const char* src, int srcLen, int* pos);
// True when the buffer starts with a UTF-8 BOM (EF BB BF).
bool utf8_has_bom(const char* src, int srcLen);

// ----------------------------------------------------------------- files ----
// Reads the whole file. On success *out receives a heap block with an extra
// trailing NUL byte (free with xfree), *outSize the byte count without it.
// Returns false when the file cannot be opened or read.
bool file_read_all(const wchar_t* path, char** out, int* outSize);
// Writes exactly 'size' bytes, creating or truncating the file.
bool file_write_all(const wchar_t* path, const char* data, int size);
bool file_exists(const wchar_t* path);

// ---------------------------------------------------------------- strings ----
int  wlen(const wchar_t* s);            // lstrlenW equivalent, NULL-safe
bool weq(const wchar_t* a, const wchar_t* b);
bool weq_ci(const wchar_t* a, const wchar_t* b);
bool weq_prefix_ci(const wchar_t* text, const wchar_t* prefix);
void wcopy(wchar_t* dst, int dstCap, const wchar_t* src);

#endif // ICSG_BASE_H
