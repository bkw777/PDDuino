void tpddWrite(uint8_t value) {
  txChecksum += value;
  CLIENT.write(value);
}

void tpddSendChecksum() {
  CLIENT.write(static_cast<uint8_t>(txChecksum ^ 0xFF));
  txChecksum = 0;
}

void return_normal(uint8_t error) {
  tpddWrite(RET_NORMAL);
  tpddWrite(1);
  tpddWrite(error);
  tpddSendChecksum();
}

// No paths, pseudo-directories, wildcards or non-ASCII FAT names are accepted.
// PC names outside the TPDD name field are exposed by their FAT short alias.
bool validName(const char* name) {
  if (!name[0] || name[0] == ' ' || !strcmp(name, ".") || !strcmp(name, "..")) return false;
  uint8_t n = 0;
  while (name[n]) {
    uint8_t c = name[n++];
    if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    if (n > WIRE_NAME_SZ) return false;
  }
  return name[n - 1] != ' ' && name[n - 1] != '.';
}

// Decode in place or into a different buffer. Only TPDD base-name padding
// immediately before the dot is removed; spaces inside a PC name survive.
bool decodeName(const uint8_t* source, char* dest) {
  uint8_t n = 0;
  while (n < WIRE_NAME_SZ && source[n]) ++n;
  while (n && source[n - 1] == ' ') --n;
  uint8_t out = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (source[i] == '.' && i == 6) {
      while (out && dest[out - 1] == ' ') --out;
    }
    dest[out++] = source[i];
  }
  dest[out] = 0;
  return validName(dest);
}

// Does this name survive TPDD's six-character base padding and 24-byte field?
bool wireNameFits(const char* name) {
  if (!validName(name)) return false;
  const char* dot = strchr(name, '.');
  size_t base = dot ? static_cast<size_t>(dot - name) : strlen(name);
  if (dot && base && name[base - 1] == ' ') return false;
  size_t padded = strlen(name) + (base < 6 ? 6 - base : 0);
  return padded <= WIRE_NAME_SZ;
}

void returnReference(const char* name, uint32_t size) {
  tpddWrite(RET_DIRECTORY);
  tpddWrite(0x1C);
  uint8_t sent = 0;
  if (name) {
    const char* dot = strchr(name, '.');
    size_t base = dot ? static_cast<size_t>(dot - name) : strlen(name);
    for (size_t i = 0; i < base; ++i) { tpddWrite(name[i]); ++sent; }
    while (sent < 6) { tpddWrite(' '); ++sent; }
    if (dot) {
      while (*dot) { tpddWrite(*dot++); ++sent; }
    }
  }
  while (sent++ < WIRE_NAME_SZ) tpddWrite(0);
  tpddWrite(0);  // Attribute
  uint16_t wireSize = size > 0xFFFFUL ? 0xFFFF : static_cast<uint16_t>(size);
  tpddWrite(wireSize >> 8);
  tpddWrite(wireSize & 0xFF);
  tpddWrite(0x80);  // Free-sector compatibility value, as in upstream
  tpddSendChecksum();
}

// Use the packet buffer to obtain the entire FAT name, so a truncated long
// name can never accidentally reference a different file on the PC's card.
bool selectEntryName() {
  char* name = reinterpret_cast<char*>(packet);
  if (!entry.getName(name, sizeof(packet))) return false;
  if (!wireNameFits(name)) {
    if (!entry.getSFN(name) || !wireNameFits(name)) return false;
  }
  strcpy(selectedName, name);
  return true;
}
