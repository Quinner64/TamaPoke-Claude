// Minimal Print: enough for Arduino_GFX (which is a Print) on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

class __FlashStringHelper;
class String;

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buf, size_t n) {
    size_t k = 0;
    while (n--) k += write(*buf++);
    return k;
  }
  size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(const __FlashStringHelper *s) { return write((const char *)s); }
  size_t print(const String &s);
  size_t print(int v) { return printf("%d", v); }
  size_t print(unsigned v) { return printf("%u", v); }
  size_t print(long v) { return printf("%ld", v); }
  size_t print(unsigned long v) { return printf("%lu", v); }
  size_t print(double v, int d = 2) { return printf("%.*f", d, v); }
  size_t println(const char *s = "") { return print(s) + print('\n'); }
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return write(buf);
  }
};
