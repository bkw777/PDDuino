void setup() {
  pinMode(DSR_PIN, INPUT_PULLUP);
  CLIENT.begin(SERIAL_BAUD);
  mountCard();  // A missing card is retried by loop(), without blocking startup.
}
