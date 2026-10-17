// Host tests compile the real firmware tabs; only UART/GPIO/SD hardware is faked.
#include "fakes/Arduino.h"
#include "fakes/SdFat.h"
#include <cassert>
#include <iostream>
#include <unistd.h>
#define PDDUINO_HOST_TEST
#include "../PDDuino/PDDuino.ino"
// Arduino generates these forward declarations when compiling the sketch.
void return_normal(uint8_t);
void returnReference(const char*, uint32_t);
#include "../PDDuino/0_system.ino"
#include "../PDDuino/1_tpdd_lib.ino"
#include "../PDDuino/2_send_loader.ino"
#include "../PDDuino/3_tpdd_commands.ino"
#include "../PDDuino/setup.ino"
#include "../PDDuino/loop.ino"

using Bytes = std::vector<uint8_t>;
void put(const std::string& name, const std::string& content) {
  std::ofstream f(fake::root / name, std::ios::binary); f << content;
}
std::string get(const std::string& name) {
  std::ifstream f(fake::root / name, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
Bytes wire(const std::string& name) {
  Bytes result(24, 0);
  assert(name.size() <= 24);
  std::copy(name.begin(), name.end(), result.begin());
  return result;
}
Bytes reference(const std::string& name, uint8_t search = SF_NAME) {
  Bytes data = wire(name); data.push_back(0); data.push_back(search); return data;
}
Bytes frame(uint8_t command, const Bytes& payload) {
  assert(payload.size() <= 255);
  Bytes data{'Z', 'Z', command, static_cast<uint8_t>(payload.size())};
  data.insert(data.end(), payload.begin(), payload.end());
  uint8_t sum = 0; for (size_t i = 2; i < data.size(); ++i) sum += data[i];
  data.push_back(sum ^ 0xFF); return data;
}
Bytes request(uint8_t command, const Bytes& payload = {}) {
  auto data = frame(command, payload); Serial.tx.clear();
  Serial.rx.insert(Serial.rx.end(), data.begin(), data.end()); loop();
  auto result = Serial.tx;
  assert(result.size() >= 3 && result.size() == static_cast<size_t>(result[1]) + 3);
  uint8_t sum = 0; for (auto c : result) sum += c; assert(sum == 0xFF);
  return result;
}
void normal(uint8_t command, const Bytes& data, uint8_t error = ERR_SUCCESS) {
  auto result = request(command, data);
  assert(result[0] == RET_NORMAL && result[1] == 1 && result[2] == error);
}
void select(const std::string& name, bool exists = true) {
  auto r = request(CMD_REFERENCE, reference(name));
  assert(r[0] == RET_DIRECTORY && r[1] == 28);
  assert((r[2] != 0) == exists);
}
void reset() {
  entry.close(); mounted = false; selectedName[0] = 0; openMode = 0;
  fake::present = true; fake::failSync = fake::failRead = fake::failWrite = false;
  Serial.rx.clear(); Serial.tx.clear(); dsrLevel = HIGH; bootstrapArmed = true;
  assert(mountCard());
}
int main() {
  char temp[] = "/tmp/pdduino-tests-XXXXXX";
  char* root = mkdtemp(temp); assert(root); fake::root = root;
  setup(); assert(Serial.baud == 19200 && mounted);
  unsigned tests = 0;

  // PC -> Model 100, including an internal space and more than six base chars.
  std::string text(300, 'X'); text[129] = '\n'; put("Long Filename.DO", text);
  select("Long Filename.DO");
  normal(CMD_STATUS, {});  // Status must not discard the selected file.
  normal(CMD_OPEN, {F_OPEN_READ});
  std::string read;
  for (int i = 0; i < 3; ++i) {
    auto r = request(CMD_READ); assert(r[0] == RET_READ);
    read.append(r.begin() + 2, r.end() - 1);
  }
  assert(read == text); normal(CMD_READ, {}, ERR_EOF); normal(CMD_CLOSE, {}); ++tests;

  // Model 100 -> PC: maximum-length packet, binary payload, write-through sync.
  select("NEW   .DO", false); normal(CMD_OPEN, {F_OPEN_WRITE});
  Bytes binary(255); for (size_t i = 0; i < binary.size(); ++i) binary[i] = i;
  auto syncs = fake::syncCount; normal(CMD_WRITE, binary); assert(fake::syncCount == syncs + 1);
  assert(get("NEW.DO") == std::string(binary.begin(), binary.end()));
  normal(CMD_CLOSE, {}); select("NEW   .DO"); normal(CMD_OPEN, {F_OPEN_APPEND});
  normal(CMD_WRITE, {'!'}); normal(CMD_CLOSE, {}); assert(get("NEW.DO").size() == 256); ++tests;

  // Rename/delete preserve LFN, reject collisions and stay inside the root.
  normal(CMD_RENAME, wire("Renamed File.DO")); assert(fake::fs::exists(fake::root / "Renamed File.DO"));
  normal(CMD_RENAME, wire("Long Filename.DO"), ERR_EXISTS);
  normal(CMD_DELETE, {}); assert(!fake::fs::exists(fake::root / "Renamed File.DO"));
  normal(CMD_REFERENCE, reference("../escape.DO"), ERR_PARM);
  normal(CMD_REFERENCE, reference("folder/file.DO"), ERR_PARM); ++tests;

  // Full-width names get a terminator outside the 24-byte wire field.
  std::string full = "ABCDEFGHIJKLMNOPQRSTU.DO"; assert(full.size() == 24);
  select(full, false); normal(CMD_OPEN, {F_OPEN_WRITE}); normal(CMD_CLOSE, {});
  select(full); normal(CMD_RENAME, wire("QRSTUVWXYZABCDEFGHIJK.DO")); ++tests;

  // Root-only enumeration skips many directories iteratively, not recursively.
  for (int i = 0; i < 300; ++i) fake::fs::create_directory(fake::root / ("000dir" + std::to_string(i)));
  put(".hidden", "ignore");
  std::string veryLong = "A filename longer than the TPDD field.DO";
  put(veryLong, "alias target"); fake::aliases["AFILEN~1.DO"] = veryLong;
  auto listing = request(CMD_REFERENCE, reference("", SF_FIRST));
  bool sawLong = false, sawAlias = false; unsigned listed = 0;
  while (listing[2]) {
    assert(++listed < 20);
    char decoded[25]; assert(decodeName(listing.data() + 2, decoded));
    assert(std::string(decoded).find("000dir") == std::string::npos);
    sawLong |= std::string(decoded) == "Long Filename.DO";
    sawAlias |= std::string(decoded) == "AFILEN~1.DO";
    listing = request(CMD_REFERENCE, reference("", SF_NEXT));
  }
  assert(sawLong && sawAlias); select("AFILEN~1.DO"); normal(CMD_OPEN, {F_OPEN_READ});
  auto aliasData = request(CMD_READ); assert(std::string(aliasData.begin()+2, aliasData.end()-1) == "alias target");
  normal(CMD_CLOSE, {}); ++tests;

  // A filename lookup moves SdFat's root cursor but must not restart a listing.
  auto first = request(CMD_REFERENCE, reference("", SF_FIRST));
  char firstName[25]; assert(decodeName(first.data()+2, firstName));
  select("Long Filename.DO");
  auto next = request(CMD_REFERENCE, reference("", SF_NEXT));
  char nextName[25]; assert(decodeName(next.data()+2, nextName));
  assert(std::string(firstName) != nextName); ++tests;

  // Packet checksum, short payloads, partial frame timeout and resynchronization.
  auto bad = frame(CMD_DELETE, {}); bad.back() ^= 1;
  Serial.tx.clear(); Serial.rx.insert(Serial.rx.end(), bad.begin(), bad.end()); loop();
  assert(Serial.tx[2] == ERR_PARM && fake::fs::exists(fake::root / veryLong));
  normal(CMD_REFERENCE, Bytes(25), ERR_PARM); normal(CMD_OPEN, {}, ERR_PARM);
  Serial.rx = {'Z', 'Z', CMD_WRITE, 20, 1}; loop(); nowMs += PACKET_TIMEOUT_MS + 1; loop();
  normal(CMD_STATUS, {}); normal(0xFE, {}, ERR_PARM); ++tests;

  // Bootstrap is one send per DSR assertion, leaves UART at 19200 and accepts
  // TPDD commands immediately afterward. It shares the normal transfer handle.
  reset(); put(LOADER_FILE, "10 PRINT \"HELLO\"\r"); dsrLevel = LOW; loop();
  std::string expected = get(LOADER_FILE) + static_cast<char>(BASIC_EOF);
  assert(std::string(Serial.tx.begin(), Serial.tx.end()) == expected);
  assert(!entry.isOpen() && Serial.baud == 19200); auto sent = Serial.tx.size(); loop(); assert(Serial.tx.size() == sent);
  normal(CMD_STATUS, {}); dsrLevel = HIGH; loop(); dsrLevel = LOW; Serial.tx.clear(); loop();
  assert(std::string(Serial.tx.begin(), Serial.tx.end()) == expected);
  dsrLevel = HIGH; loop(); ++tests;

  // No card at boot: service stays alive, retries, and can bootstrap on insertion.
  reset(); fake::present = false; mounted = false; dsrLevel = LOW;
  nowMs += CARD_RETRY_MS + 1; Serial.tx.clear(); loop(); assert(!mounted && Serial.tx.empty());
  fake::present = true; nowMs += CARD_RETRY_MS + 1; loop();
  assert(mounted && std::string(Serial.tx.begin(), Serial.tx.end()) == expected); dsrLevel = HIGH; loop(); ++tests;

  // Sync/read failures are errors, never false successful writes/EOF.
  reset(); select("FAILED.DO", false); normal(CMD_OPEN, {F_OPEN_WRITE});
  fake::failSync = true; normal(CMD_WRITE, {'x'}, ERR_DISK_FULL); assert(!mounted && !entry.isOpen());
  reset(); select("Long Filename.DO"); normal(CMD_OPEN, {F_OPEN_READ});
  fake::failRead = true; normal(CMD_READ, {}, ERR_READ_TIMEOUT); assert(!mounted); ++tests;

  // Refresh listing after a PC card exchange, without resetting the Model 100.
  reset(); auto previous = fake::root; fake::root = previous / "replacement";
  fake::fs::create_directory(fake::root); put("NEWCARD.DO", "new card");
  listing = request(CMD_REFERENCE, reference("", SF_FIRST));
  char decoded[25]; assert(decodeName(listing.data()+2, decoded)); assert(std::string(decoded) == "NEWCARD.DO");
  normal(CMD_OPEN, {F_OPEN_READ}); auto r = request(CMD_READ);
  assert(std::string(r.begin()+2, r.end()-1) == "new card"); normal(CMD_CLOSE, {}); ++tests;

  // TS-DOS label compatibility remains, without enabling any subdirectories.
  auto dme = request(CMD_DMEREQ); assert(dme[0] == RET_NORMAL && dme[1] == 11);
  normal(CMD_FORMAT, {}); assert(fake::fs::exists(fake::root / "NEWCARD.DO")); ++tests;

  fake::root = previous; fake::fs::remove_all(fake::root);
  std::cout << tests << " firmware scenarios passed\n";
}
