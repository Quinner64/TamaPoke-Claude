// Minimal Arduino.h for compiling Arduino_GFX's drawing core on a PC.
// Only what Arduino_GFX.cpp / Arduino_Canvas.cpp and the Claude dashboard
// (claudemon.cpp / claudeui.cpp) need. Used by tools/claude_preview/ only.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "Print.h"
#include "pgmspace.h"

typedef uint8_t byte;
typedef bool boolean;

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

uint32_t millis();
uint32_t micros();
inline void delay(uint32_t) {}
inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0

// just enough String for Arduino_GFX's getTextBounds(const String &) overload
class String {
public:
  String(const char *s = "") : s_(s ? s : "") {}
  const char *c_str() const { return s_; }
  unsigned int length() const { return (unsigned int)strlen(s_); }
  char charAt(unsigned int i) const { return s_[i]; }
  char operator[](unsigned int i) const { return s_[i]; }
private:
  const char *s_;
};
inline size_t Print::print(const String &s) { return write(s.c_str()); }
#define F(s) ((const __FlashStringHelper *)(s))

using std::min;
using std::max;
template <class T, class L, class H> static inline T constrain(T v, L lo, H hi) {
  return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v);
}
