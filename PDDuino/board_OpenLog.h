#pragma once

// Original SparkFun OpenLog: ATmega328P, 16 MHz, hardware UART, SD CS = 10.
// DSR is an additional wire, NOT the OpenLog header's DTR/reset connection.
#ifndef DSR_PIN
#define DSR_PIN 6
#endif
#ifndef DSR_ACTIVE_LEVEL
#define DSR_ACTIVE_LEVEL LOW
#endif

#define CLIENT Serial
#define SD_CS_PIN 10
#define LOADER_FILE "LOADER.DO"
#define LOADER_MS_PER_BYTE 8
#define SERIAL_BAUD 19200
#define PACKET_TIMEOUT_MS 500UL
#define CARD_RETRY_MS 1000UL
