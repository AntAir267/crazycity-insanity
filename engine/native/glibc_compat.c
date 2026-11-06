/* The native game for older Linux systems: this computer's glibc (2.43) gives some functions new
   symbol versions (correctly rounded float maths in 2.43, C23 number parsing, strlcpy and
   arc4random in 2.36 - 2.38), so a program linked here would not start on a glibc older than
   that. These definitions take their place in the program (for our code, SDL and the static
   libstdc++ alike) and call the versions every x86-64 glibc has, or do the work themselves.
   With them the program needs glibc 2.34 (where libpthread and libdl joined libc).

   C, not C++, and without _GNU_SOURCE, so no header renames these functions. */
#if defined(__linux__)
#include <features.h>
#endif
#if defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__)

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

/* The definitions are hidden: the program's own code and its static libraries use them, but they
   are not exported, so the dynamic linker never binds the old-version references (or libm's own
   calls) to them. Exported, the old-version reference of acosf would find this acosf again. */
#define CC_HIDDEN __attribute__((visibility("hidden")))

/* Maths: the GLIBC_2.2.5 versions. */
#define CC_OLD_F1(name)                                     \
    float cc_old_##name(float);                             \
    __asm__(".symver cc_old_" #name ", " #name "@GLIBC_2.2.5"); \
    CC_HIDDEN float name(float x) { return cc_old_##name(x); }
#define CC_OLD_F2(name)                                       \
    float cc_old_##name(float, float);                        \
    __asm__(".symver cc_old_" #name ", " #name "@GLIBC_2.2.5"); \
    CC_HIDDEN float name(float x, float y) { return cc_old_##name(x, y); }

CC_OLD_F1(acosf)
CC_OLD_F1(asinf)
CC_OLD_F1(log10f)
CC_OLD_F1(sqrtf)
CC_OLD_F2(atan2f)
CC_OLD_F2(remainderf)
CC_OLD_F2(fmodf)

double cc_old_fmod(double, double);
__asm__(".symver cc_old_fmod, fmod@GLIBC_2.2.5");
CC_HIDDEN double fmod(double x, double y) { return cc_old_fmod(x, y); }

/* C23 number parsing (glibc 2.38): the C99 functions (no 0b prefix, which nothing here reads). */
long cc_old_strtol(const char*, char**, int);
long long cc_old_strtoll(const char*, char**, int);
unsigned long cc_old_strtoul(const char*, char**, int);
unsigned long long cc_old_strtoull(const char*, char**, int);
long cc_old_wcstol(const wchar_t*, wchar_t**, int);
int cc_old_vsscanf(const char*, const char*, va_list);
int cc_old_vfscanf(FILE*, const char*, va_list);
__asm__(".symver cc_old_strtol, strtol@GLIBC_2.2.5");
__asm__(".symver cc_old_strtoll, strtoll@GLIBC_2.2.5");
__asm__(".symver cc_old_strtoul, strtoul@GLIBC_2.2.5");
__asm__(".symver cc_old_strtoull, strtoull@GLIBC_2.2.5");
__asm__(".symver cc_old_wcstol, wcstol@GLIBC_2.2.5");
__asm__(".symver cc_old_vsscanf, __isoc99_vsscanf@GLIBC_2.7");
__asm__(".symver cc_old_vfscanf, __isoc99_vfscanf@GLIBC_2.7");

CC_HIDDEN long __isoc23_strtol(const char* s, char** e, int b) { return cc_old_strtol(s, e, b); }
CC_HIDDEN long long __isoc23_strtoll(const char* s, char** e, int b) { return cc_old_strtoll(s, e, b); }
CC_HIDDEN unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return cc_old_strtoul(s, e, b); }
CC_HIDDEN unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return cc_old_strtoull(s, e, b); }
CC_HIDDEN long __isoc23_wcstol(const wchar_t* s, wchar_t** e, int b) { return cc_old_wcstol(s, e, b); }
CC_HIDDEN int __isoc23_vsscanf(const char* s, const char* f, va_list ap) { return cc_old_vsscanf(s, f, ap); }
CC_HIDDEN int __isoc23_sscanf(const char* s, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    const int r = cc_old_vsscanf(s, f, ap);
    va_end(ap);
    return r;
}
CC_HIDDEN int __isoc23_fscanf(FILE* fp, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    const int r = cc_old_vfscanf(fp, f, ap);
    va_end(ap);
    return r;
}

/* BSD string copies (glibc 2.38). */
CC_HIDDEN size_t strlcpy(char* d, const char* s, size_t n) {
    size_t len = 0;
    while (s[len]) ++len;
    if (n) {
        const size_t c = len < n - 1 ? len : n - 1;
        for (size_t i = 0; i < c; ++i) d[i] = s[i];
        d[c] = 0;
    }
    return len;
}
CC_HIDDEN size_t strlcat(char* d, const char* s, size_t n) {
    size_t dl = 0;
    while (dl < n && d[dl]) ++dl;
    if (dl == n) {
        size_t sl = 0;
        while (s[sl]) ++sl;
        return n + sl;
    }
    return dl + strlcpy(d + dl, s, n - dl);
}
CC_HIDDEN size_t wcslcpy(wchar_t* d, const wchar_t* s, size_t n) {
    size_t len = 0;
    while (s[len]) ++len;
    if (n) {
        const size_t c = len < n - 1 ? len : n - 1;
        for (size_t i = 0; i < c; ++i) d[i] = s[i];
        d[c] = 0;
    }
    return len;
}
CC_HIDDEN size_t wcslcat(wchar_t* d, const wchar_t* s, size_t n) {
    size_t dl = 0;
    while (dl < n && d[dl]) ++dl;
    if (dl == n) {
        size_t sl = 0;
        while (s[sl]) ++sl;
        return n + sl;
    }
    return dl + wcslcpy(d + dl, s, n - dl);
}

/* arc4random (glibc 2.36; std::random_device uses it): the kernel's getrandom. */
CC_HIDDEN unsigned int arc4random(void) {
    unsigned int v = 0;
    unsigned char* p = (unsigned char*)&v;
    size_t got = 0;
    while (got < sizeof v) {
        const long r = syscall(SYS_getrandom, p + got, sizeof v - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return v;
}

#endif
