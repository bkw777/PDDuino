#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#define USE_LONG_FILE_NAMES 1
using oflag_t = uint8_t;
constexpr oflag_t O_READ = 1, O_WRITE = 2, O_CREAT = 4, O_TRUNC = 8, O_APPEND = 16;
namespace fake {
namespace fs = std::filesystem;
inline fs::path root;
inline bool present = true, failSync = false, failRead = false, failWrite = false;
inline unsigned syncCount = 0;
// alias -> real filename; filesystem-native SFN generation is not emulated.
inline std::map<std::string, std::string> aliases;
}
class FatFile {
  std::fstream stream;
  fake::fs::path path;
  bool opened = false, directory = false;
  uint8_t error = 0;
  std::vector<fake::fs::path> children;
  size_t cursor = 0;
  static inline FatFile* cwd = nullptr;
 public:
  bool isOpen() const { return opened; }
  bool isFile() const { return opened && !directory; }
  bool isHidden() const { return path.filename().string()[0] == '.'; }
  void initRoot() { cwd = this; opened = directory = true; error = 0; path = fake::root; rewind(); }
  bool open(const char* name, oflag_t flags) {
    close(); error = 0;
    if (!fake::present) { error = 1; return false; }
    std::string actual = fake::aliases.count(name) ? fake::aliases.at(name) : name;
    path = fake::root / actual;
    // SdFat name lookup rewinds and advances its root, even on a miss.
    if (cwd) {
      auto it = std::find(cwd->children.begin(), cwd->children.end(), path);
      cwd->cursor = it == cwd->children.end() ? cwd->children.size() : it - cwd->children.begin() + 1;
    }
    if (!fake::fs::exists(path) && !(flags & O_CREAT)) return false;
    directory = fake::fs::is_directory(path);
    if (!directory) {
      if ((flags & O_CREAT) && !fake::fs::exists(path)) { std::ofstream create(path); }
      auto mode = std::ios::binary | std::ios::in;
      if (flags & O_WRITE) mode |= std::ios::out;
      if (flags & O_TRUNC) mode |= std::ios::trunc;
      if (flags & O_APPEND) mode |= std::ios::app;
      stream.open(path, mode);
      if (!stream) return false;
    }
    opened = true;
    return true;
  }
  bool openNext(FatFile* root, oflag_t flags) {
    if (!fake::present) { root->error = 1; return false; }
    if (root->cursor >= root->children.size()) return false;
    auto child = root->children[root->cursor++];
    return open(child.filename().string().c_str(), flags);
  }
  void rewind() {
    error = 0; children.clear(); cursor = 0;
    if (!fake::present) return;
    for (auto const& item : fake::fs::directory_iterator(fake::root)) children.push_back(item.path());
    std::sort(children.begin(), children.end());
  }
  uint32_t curPosition() const { return cursor; }
  bool seekSet(uint32_t offset) { cursor = offset; return fake::present; }
  void close() { if (stream.is_open()) stream.close(); stream.clear(); opened = false; }
  uint32_t fileSize() const { return isFile() ? fake::fs::file_size(path) : 0; }
  int read(uint8_t* buf, size_t n) {
    if (!fake::present || fake::failRead) { error = 1; return -1; }
    stream.read(reinterpret_cast<char*>(buf), n);
    return stream.gcount();
  }
  int read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
  size_t write(const uint8_t* buf, size_t n) {
    if (!fake::present || fake::failWrite) { error = 1; return 0; }
    stream.write(reinterpret_cast<const char*>(buf), n);
    return stream ? n : 0;
  }
  bool sync() {
    ++fake::syncCount;
    if (!fake::present || fake::failSync) { error = 1; return false; }
    stream.flush(); return true;
  }
  uint8_t getError() const { return error; }
  bool getName(char* out, size_t size) {
    auto name = path.filename().string();
    std::strncpy(out, name.c_str(), size - 1); out[size - 1] = 0; return true;
  }
  bool getSFN(char* out) {
    for (auto const& item : fake::aliases) {
      if (item.second == path.filename()) { std::strcpy(out, item.first.c_str()); return true; }
    }
    return false;
  }
  bool remove() { auto p = path; close(); return fake::present && fake::fs::remove(p); }
  bool rename(const char* name) {
    if (!fake::present) return false;
    stream.flush(); fake::fs::rename(path, fake::root / name); path = fake::root / name; return true;
  }
};
class SdFat {
  FatFile root;
 public:
  bool begin(int pin) { if (pin != 10 || !fake::present) return false; root.initRoot(); return true; }
  void chvol() {}
  FatFile* vwd() { return &root; }
  bool exists(const char* name) { return fake::fs::exists(fake::root / name); }
};
