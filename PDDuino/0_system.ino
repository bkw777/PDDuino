// Discard stale handles after an I/O error. Do not reuse a dirty cache on a
// different card. SdFat::begin() initializes a fresh cache on the next mount.
void invalidateCard() {
  entry.close();
  openMode = 0;
  mounted = false;
  selectedName[0] = 0;
}

bool mountCard() {
  if (entry.isOpen()) return false;
  lastCardAttempt = millis();
  selectedName[0] = 0;
  nextDirectoryPosition = 0;
  mounted = SD.begin(SD_CS_PIN);
  if (mounted) {
    SD.chvol();
    SD.vwd()->rewind();
  }
  return mounted;
}

bool ensureCard() {
  return mounted || mountCard();
}

void storageError(uint8_t error) {
  invalidateCard();
  return_normal(error);
}
