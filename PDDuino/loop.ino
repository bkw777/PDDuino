void loop() {
  // Fixed-size parser: payload length is at most 255; the shared buffer is 256.
  enum State : uint8_t { IDLE, Z, COMMAND, LENGTH, PAYLOAD };
  static State state = IDLE;
  static uint8_t command = 0;
  static uint8_t received = 0;
  static uint8_t sum = 0;
  static unsigned long lastByte = 0;

  if (state != IDLE && millis() - lastByte > PACKET_TIMEOUT_MS) state = IDLE;
  if (state == IDLE && !CLIENT.available()) {
    if (!mounted && millis() - lastCardAttempt >= CARD_RETRY_MS) mountCard();
    pollBootstrap();
  }
  while (CLIENT.available()) {
    uint8_t value = CLIENT.read();
    lastByte = millis();
    switch (state) {
      case IDLE: if (value == 'Z') state = Z; break;
      case Z: state = value == 'Z' ? COMMAND : IDLE; break;
      case COMMAND: command = value; sum = value; state = LENGTH; break;
      case LENGTH:
        payloadLength = value; received = 0; sum += value; state = PAYLOAD; break;
      case PAYLOAD:
        sum += value;
        if (received < payloadLength) packet[received++] = value;
        else {
          if (sum == 0xFF) {
            bootstrapArmed = false;  // Normal TPDD traffic never initiates a loader.
            dispatchCommand(command);
          } else return_normal(ERR_PARM);
          state = IDLE;
        }
        break;
    }
  }
}
