#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
using byte = uint8_t;
constexpr int LOW = 0, HIGH = 1, INPUT_PULLUP = 2;
inline unsigned long nowMs = 0;
inline int dsrLevel = HIGH;
inline unsigned long millis() { return nowMs; }
inline void delay(unsigned long n) { nowMs += n; }
inline void pinMode(int, int) {}
inline int digitalRead(int) { return dsrLevel; }
struct FakeSerial {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  unsigned long baud = 0;
  void begin(unsigned long n) { baud = n; }
  int available() { return rx.size(); }
  int read() { int n = rx.front(); rx.pop_front(); return n; }
  size_t write(uint8_t n) { tx.push_back(n); return 1; }
  void flush() {}
};
inline FakeSerial Serial;
