// DSR must be manually wired to DSR_PIN through the appropriate signal-level
// interface. The OpenLog programming header's DTR is RESET, not this input.
// Reuse the transfer handle and return to TPDD without restarting the UART.
void sendLoader() {
  if (!entry.open(LOADER_FILE, O_READ)) return;
  if (!entry.isFile()) { entry.close(); return; }
  int value;
  while ((value = entry.read()) >= 0) {
    CLIENT.write(static_cast<uint8_t>(value));
    delay(LOADER_MS_PER_BYTE);
  }
  bool failed = entry.getError() != 0;
  entry.close();
  if (failed) { invalidateCard(); return; }
  CLIENT.write(BASIC_EOF);
  CLIENT.flush();
}

void pollBootstrap() {
  if (digitalRead(DSR_PIN) != DSR_ACTIVE_LEVEL) {
    bootstrapArmed = true;
    return;
  }
  if (bootstrapArmed && mounted && !entry.isOpen()) {
    bootstrapArmed = false;  // One send per assertion, including a missing file.
    selectedName[0] = 0;
    sendLoader();
  }
}
