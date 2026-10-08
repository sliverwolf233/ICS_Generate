// base.cpp - CRT-free runtime: process heap, WStr, UTF-8 codec, file I/O.
// Contract: src/core/base.h (frozen). No CRT/STL headers, no exceptions, no RTTI.
#include "base.h"

// ------------------------------------------------------------------ heap ----

static void icsg_oom(void) {
    // The contract says "never returns NULL for n>0": fail fast instead.
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) { }
}

void* xmalloc(size_t bytes) {
    if (bytes == 0) return NULL;
    void* p = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!p) icsg_oom();
    return p;
}

void* xrealloc(void* block, size_t bytes) {
    if (!block) return xmalloc(bytes);
    if (bytes == 0) { HeapFree(GetProcessHeap(), 0, block); return NULL; }
    void* p = HeapReAlloc(GetProcessHeap(), 0, block, bytes);
    if (!p) icsg_oom();
    return p;
}

void xfree(void* block) {
    if (block) HeapFree(GetProcessHeap(), 0, block);
}

void xmemzero(void* dst, size_t bytes) {
    if (!dst || bytes == 0) return;
    unsigned char* p = (unsigned char*)dst;
    while (bytes--) *p++ = 0;
}

void xmemcopy(void* dst, const void* src, size_t bytes) {
    if (!dst || !src || bytes == 0) return;
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (bytes--) *d++ = *s++;
}

void xmemmove(void* dst, const void* src, size_t bytes) {
    if (!dst || !src || bytes == 0) return;
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s) return;
    if (d < s) {
        while (bytes--) *d++ = *s++;
    } else {
        d += bytes;
        s += bytes;
        while (bytes--) *--d = *--s;
    }
}

bool xmemeq(const void* a, const void* b, size_t bytes) {
    if (a == b) return true;
    if (!a || !b) return false;
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    while (bytes--) {
        if (*pa++ != *pb++) return false;
    }
    return true;
}

// ------------------------------------------------------------------ WStr ----

static wchar_t icsg_lower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c;
}

void wstr_init(WStr* s) {
    if (!s) return;
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void wstr_free(WStr* s) {
    if (!s) return;
    if (s->data) xfree(s->data);
    wstr_init(s);
}

void wstr_clear(WStr* s) {
    if (!s) return;
    s->len = 0;
    if (s->data) s->data[0] = 0;
}

bool wstr_reserve(WStr* s, int needCap) {
    if (!s) return false;
    if (needCap <= 0) return true;
    if (s->data && s->cap >= needCap) return true;
    int newCap = s->cap;
    if (newCap < 16) newCap = 16;
    while (newCap < needCap) {
        if (newCap > 0x20000000) { newCap = needCap; break; }
        newCap *= 2;
    }
    wchar_t* grown = (wchar_t*)xrealloc(s->data, (size_t)newCap * sizeof(wchar_t));
    if (!grown) return false;
    s->data = grown;
    s->cap = newCap;
    if (s->len < 0) s->len = 0;
    if (s->len >= s->cap) s->len = s->cap - 1;
    s->data[s->len] = 0;
    return true;
}

void wstr_set_n(WStr* s, const wchar_t* src, int count) {
    if (!s) return;
    if (!src || count <= 0) { wstr_clear(s); return; }
    if (!wstr_reserve(s, count + 1)) return;
    xmemcopy(s->data, src, (size_t)count * sizeof(wchar_t));
    s->len = count;
    s->data[count] = 0;
}

void wstr_set(WStr* s, const wchar_t* src) {
    wstr_set_n(s, src, wlen(src));
}

void wstr_append_n(WStr* s, const wchar_t* src, int count) {
    if (!s || !src || count <= 0) return;
    // Support self-append: the source may live inside this string.
    ULONG_PTR srcAddr = (ULONG_PTR)(const void*)src;
    ULONG_PTR dataAddr = (ULONG_PTR)(const void*)s->data;
    int offset = -1;
    if (s->data && srcAddr >= dataAddr && srcAddr <= dataAddr + (ULONG_PTR)s->len * sizeof(wchar_t)) {
        offset = (int)((srcAddr - dataAddr) / sizeof(wchar_t));
    }
    if (!wstr_reserve(s, s->len + count + 1)) return;
    if (offset >= 0) src = s->data + offset;
    xmemcopy(s->data + s->len, src, (size_t)count * sizeof(wchar_t));
    s->len += count;
    s->data[s->len] = 0;
}

void wstr_append(WStr* s, const wchar_t* src) {
    wstr_append_n(s, src, wlen(src));
}

void wstr_append_ch(WStr* s, wchar_t ch) {
    if (!s) return;
    if (!wstr_reserve(s, s->len + 2)) return;
    s->data[s->len++] = ch;
    s->data[s->len] = 0;
}

void wstr_append_num(WStr* s, long long value, int minDigits) {
    if (!s) return;
    wchar_t buf[32];
    int n = 0;
    unsigned long long mag = (value < 0)
        ? (0ULL - (unsigned long long)value)
        : (unsigned long long)value;
    if (mag == 0) buf[n++] = L'0';
    while (mag > 0 && n < ICSG_ARRAY_COUNT(buf)) {
        buf[n++] = (wchar_t)(L'0' + (int)(mag % 10ULL));
        mag /= 10ULL;
    }
    while (n < minDigits && n < ICSG_ARRAY_COUNT(buf)) buf[n++] = L'0';
    if (value < 0 && wstr_reserve(s, s->len + n + 2)) {
        s->data[s->len++] = L'-';
        s->data[s->len] = 0;
    }
    while (n > 0) wstr_append_ch(s, buf[--n]);
}

bool wstr_is_empty(const WStr* s) {
    return !s || !s->data || s->len <= 0;
}

bool wstr_equals(const WStr* s, const wchar_t* rhs) {
    if (!s || !s->data) return !rhs || !*rhs;
    if (!rhs) return s->len == 0;
    if (s->len != wlen(rhs)) return false;
    for (int i = 0; i < s->len; ++i) {
        if (s->data[i] != rhs[i]) return false;
    }
    return true;
}

bool wstr_equals_ci(const WStr* s, const wchar_t* rhs) {
    if (!s || !s->data) return !rhs || !*rhs;
    if (!rhs) return s->len == 0;
    if (s->len != wlen(rhs)) return false;
    for (int i = 0; i < s->len; ++i) {
        if (icsg_lower(s->data[i]) != icsg_lower(rhs[i])) return false;
    }
    return true;
}

bool wstr_starts_ci(const wchar_t* text, int len, const wchar_t* prefix) {
    if (!prefix) return true;
    int need = wlen(prefix);
    if (!text || len < need) return false;
    for (int i = 0; i < need; ++i) {
        if (icsg_lower(text[i]) != icsg_lower(prefix[i])) return false;
    }
    return true;
}

const wchar_t* wstr_c(const WStr* s) {
    return (s && s->data) ? s->data : L"";
}

// ------------------------------------------------------------ UTF-8 codec ----

int utf8_encoded_len(const wchar_t* src, int srcLen) {
    if (!src || srcLen <= 0) return 0;
    int total = 0;
    for (int i = 0; i < srcLen; ++i) {
        unsigned int c = (unsigned int)(unsigned short)src[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= srcLen) return -1;
            unsigned int lo = (unsigned int)(unsigned short)src[i + 1];
            if (lo < 0xDC00 || lo > 0xDFFF) return -1;
            total += 4;
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            return -1;
        } else if (c < 0x80) {
            total += 1;
        } else if (c < 0x800) {
            total += 2;
        } else {
            total += 3;
        }
    }
    return total;
}

int utf8_encode(const wchar_t* src, int srcLen, char* out, int outCap) {
    if (!src || srcLen <= 0) return 0;
    if (!out || outCap <= 0) return -1;
    int n = 0;
    for (int i = 0; i < srcLen; ++i) {
        unsigned int cp = (unsigned int)(unsigned short)src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= srcLen) return -1;
            unsigned int lo = (unsigned int)(unsigned short)src[i + 1];
            if (lo < 0xDC00 || lo > 0xDFFF) return -1;
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            ++i;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return -1;
        }
        int need = (cp < 0x80u) ? 1 : (cp < 0x800u) ? 2 : (cp < 0x10000u) ? 3 : 4;
        if (n + need > outCap) return -1;
        if (need == 1) {
            out[n++] = (char)cp;
        } else if (need == 2) {
            out[n++] = (char)(0xC0u | (cp >> 6));
            out[n++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (need == 3) {
            out[n++] = (char)(0xE0u | (cp >> 12));
            out[n++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            out[n++] = (char)(0xF0u | (cp >> 18));
            out[n++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            out[n++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    return n;
}

int utf8_decode(const char* src, int srcLen, wchar_t* out, int outCap) {
    if (!src || srcLen <= 0) return 0;
    if (!out || outCap <= 0) return -1;
    int n = 0;
    int i = 0;
    while (i < srcLen) {
        unsigned char b = (unsigned char)src[i];
        unsigned int cp;
        int need;
        if (b < 0x80) { cp = b; need = 1; }
        else if ((b & 0xE0) == 0xC0) { cp = (unsigned int)(b & 0x1F); need = 2; }
        else if ((b & 0xF0) == 0xE0) { cp = (unsigned int)(b & 0x0F); need = 3; }
        else if ((b & 0xF8) == 0xF0) { cp = (unsigned int)(b & 0x07); need = 4; }
        else return -1;                       // continuation byte or 5/6 byte lead
        if (i + need > srcLen) return -1;     // truncated sequence
        for (int k = 1; k < need; ++k) {
            unsigned char c = (unsigned char)src[i + k];
            if ((c & 0xC0) != 0x80) return -1;
            cp = (cp << 6) | (unsigned int)(c & 0x3F);
        }
        if (need == 2 && cp < 0x80u) return -1;         // overlong
        if (need == 3 && cp < 0x800u) return -1;
        if (need == 4 && cp < 0x10000u) return -1;
        if (cp > 0x10FFFFu) return -1;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return -1;  // surrogate range
        if (cp < 0x10000u) {
            if (n + 1 > outCap) return -1;
            out[n++] = (wchar_t)cp;
        } else {
            if (n + 2 > outCap) return -1;
            cp -= 0x10000u;
            out[n++] = (wchar_t)(0xD800u + (cp >> 10));
            out[n++] = (wchar_t)(0xDC00u + (cp & 0x3FFu));
        }
        i += need;
    }
    return n;
}

wchar_t utf8_next(const char* src, int srcLen, int* pos) {
    if (!src || !pos) return (wchar_t)0xFFFD;
    int i = *pos;
    if (i < 0 || i >= srcLen) return (wchar_t)0xFFFD;
    unsigned char b = (unsigned char)src[i];
    unsigned int cp;
    int need;
    if (b < 0x80) { cp = b; need = 1; }
    else if ((b & 0xE0) == 0xC0) { cp = (unsigned int)(b & 0x1F); need = 2; }
    else if ((b & 0xF0) == 0xE0) { cp = (unsigned int)(b & 0x0F); need = 3; }
    else if ((b & 0xF8) == 0xF0) { cp = (unsigned int)(b & 0x07); need = 4; }
    else { *pos = i + 1; return (wchar_t)0xFFFD; }
    if (i + need > srcLen) { *pos = i + 1; return (wchar_t)0xFFFD; }
    for (int k = 1; k < need; ++k) {
        unsigned char c = (unsigned char)src[i + k];
        if ((c & 0xC0) != 0x80) { *pos = i + 1; return (wchar_t)0xFFFD; }
        cp = (cp << 6) | (unsigned int)(c & 0x3F);
    }
    if ((need == 2 && cp < 0x80u) || (need == 3 && cp < 0x800u) ||
        (need == 4 && cp < 0x10000u) || cp > 0x10FFFFu ||
        (cp >= 0xD800u && cp <= 0xDFFFu)) {
        *pos = i + 1;
        return (wchar_t)0xFFFD;
    }
    *pos = i + need;
    if (cp < 0x10000u) return (wchar_t)cp;
    // Astral code point: the caller sees the leading surrogate of the pair.
    return (wchar_t)(0xD800u + ((cp - 0x10000u) >> 10));
}

bool utf8_has_bom(const char* src, int srcLen) {
    if (!src || srcLen < 3) return false;
    return (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB &&
           (unsigned char)src[2] == 0xBF;
}

// ----------------------------------------------------------------- files ----

bool file_read_all(const wchar_t* path, char** out, int* outSize) {
    if (!path || !out || !outSize) return false;
    *out = NULL;
    *outSize = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x7FFFFFF0LL) {
        CloseHandle(h);
        return false;
    }
    int total = (int)size.QuadPart;
    char* buf = (char*)xmalloc((size_t)total + 1);
    int got = 0;
    while (got < total) {
        DWORD chunk = (DWORD)(total - got);
        DWORD read = 0;
        if (!ReadFile(h, buf + got, chunk, &read, NULL) || read == 0) break;
        got += (int)read;
    }
    CloseHandle(h);
    if (got != total) { xfree(buf); return false; }
    buf[total] = 0;
    *out = buf;
    *outSize = total;
    return true;
}

bool file_write_all(const wchar_t* path, const char* data, int size) {
    if (!path || size < 0 || (!data && size > 0)) return false;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    int wrote = 0;
    bool ok = true;
    while (wrote < size) {
        DWORD chunk = (DWORD)(size - wrote);
        DWORD n = 0;
        if (!WriteFile(h, data + wrote, chunk, &n, NULL) || n == 0) { ok = false; break; }
        wrote += (int)n;
    }
    CloseHandle(h);
    return ok && wrote == size;
}

bool file_exists(const wchar_t* path) {
    if (!path || !*path) return false;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// --------------------------------------------------------------- strings ----

int wlen(const wchar_t* s) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) ++n;
    return n;
}

bool weq(const wchar_t* a, const wchar_t* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

bool weq_ci(const wchar_t* a, const wchar_t* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && icsg_lower(a[i]) == icsg_lower(b[i])) ++i;
    return icsg_lower(a[i]) == icsg_lower(b[i]);
}

bool weq_prefix_ci(const wchar_t* text, const wchar_t* prefix) {
    if (!prefix) return true;
    if (!text) return *prefix == 0;
    int i = 0;
    while (prefix[i]) {
        if (!text[i] || icsg_lower(text[i]) != icsg_lower(prefix[i])) return false;
        ++i;
    }
    return true;
}

void wcopy(wchar_t* dst, int dstCap, const wchar_t* src) {
    if (!dst || dstCap <= 0) return;
    int i = 0;
    if (src) {
        while (i < dstCap - 1 && src[i]) { dst[i] = src[i]; ++i; }
    }
    dst[i] = 0;
}
