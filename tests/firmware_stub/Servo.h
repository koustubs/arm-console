#pragma once
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

using byte = unsigned char;
using __FlashStringHelper = char;
#define F(value) (value)
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define constrain(value, lower, upper) ((value) < (lower) ? (lower) : ((value) > (upper) ? (upper) : (value)))

extern unsigned long fakeMillis;
extern unsigned long fakeEcho;
extern int fakeSwitch;
inline unsigned long millis() { return fakeMillis; }
inline void delayMicroseconds(unsigned int) {}
inline void pinMode(byte, int) {}
inline void digitalWrite(byte, int) {}
inline int digitalRead(byte) { return fakeSwitch; }
inline unsigned long pulseIn(byte, int, unsigned long) { return fakeEcho; }

class Servo {
 public:
  int angle = -1;
  int writes = 0;
  bool attached = false;
  void write(int value) { angle = value; ++writes; }
  void attach(byte) { attached = true; }
};

struct FakeSerial {
  std::string input;
  std::string output;
  void begin(int) {}
  int available() { return static_cast<int>(input.size()); }
  char read() { char c = input.front(); input.erase(0, 1); return c; }
  template <class T> void print(const T &value) { std::ostringstream out; out << value; output += out.str(); }
  void print(float value, int digits) { std::ostringstream out; out << std::fixed << std::setprecision(digits) << value; output += out.str(); }
  template <class T> void println(const T &value) { print(value); output += '\n'; }
};
extern FakeSerial Serial;
