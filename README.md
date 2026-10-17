# PDDuino for original SparkFun OpenLog

A small TPDD file server for an **ATmega328P OpenLog at 16 MHz**, mounted inside
a TRS-80/Tandy Model 100 with external access to its microSD card. Move the card
between the Model 100 and a PC to exchange development files.

This fork replaces the upstream Feather/Teensy implementation with an OpenLog
implementation. It retains **DSR-triggered bootstrap and FAT long filenames**,
but exposes **only files in the SD root**. There are no LED controls, debug
console, sleep modes, subdirectory navigation, or card-detect wiring.

Derived from [PDDuino](https://github.com/bkw777/PDDuino) and
[SD2TPDD](https://github.com/tangentdelta/SD2TPDD). GPL-3.0; see [LICENSE](LICENSE).

## Wiring

| Function | OpenLog / ATmega328P mapping |
| --- | --- |
| Model 100 serial data | Hardware UART RX / TX, 19,200 baud, 8N1 |
| SD chip select | Digital pin 10, already wired on OpenLog |
| Bootstrap DSR input | Additional wire to digital pin 6 by default; active LOW |
| Ground | Common signal ground |

Set `DSR_PIN` and `DSR_ACTIVE_LEVEL` in
[PDDuino/board_OpenLog.h](PDDuino/board_OpenLog.h) to match the manually wired
input. This input follows the original firmware's DSR convention: it senses the
Model 100's asserted DTR through the interface. **The OpenLog header's DTR pin
is connected to RESET and is not this GPIO.**

Internal installation does not imply compatible signal levels. Use the correct
level translation and polarity for the connection point in the Model 100;
RS-232 voltages must not connect directly to OpenLog GPIO. Model 100 handshake
inputs must be satisfied by the installation: this firmware has no ready/DTR
output. Do not connect a programming adapter and the Model 100 as simultaneous
UART drivers. Verify the board's power and bootloader before flashing.

SparkFun's original OpenLog firmware documents the 16 MHz clock and SD CS 10:
[hardware reference](https://github.com/sparkfun/OpenLog/blob/master/firmware/OpenLog_Firmware/OpenLog/OpenLog.ino).
Existing board LEDs on SPI signals can still show electrical bus activity;
the firmware never configures or drives LEDs as indicators.

## Build

Use **Arduino CLI 1.3.1**, **Arduino AVR core 1.8.6**, and **SdFat 1.1.4 by Bill
Greiman**, with its default long-name support enabled. SdFat 2.x uses a different
API. No Adafruit or Teensy packages are needed.

```sh
arduino-cli core update-index
arduino-cli core install arduino:avr@1.8.6
arduino-cli lib install 'SdFat@1.1.4'
./tools/build.sh
```

The Uno profile selects the matching ATmega328P/16 MHz toolchain. It is a compile
profile, not a promise that a particular OpenLog bootloader uses Uno upload
settings. The build script does **not** upload. Outputs default to
`/tmp/pdduino-openlog-build`; override `PDDUINO_BUILD_DIR` if needed.
`ARDUINO_CLI` can point to a CLI binary outside `PATH`.

For different DSR wiring without editing the configuration:

```sh
./tools/build.sh --build-property 'compiler.cpp.extra_flags=-DDSR_PIN=6 -DDSR_ACTIVE_LEVEL=LOW'
```

## Bootstrap

1. Format the microSD card as FAT16/FAT32, not exFAT. Put the intended Model 100
   ASCII BASIC loader in the root as **LOADER.DO**. Follow that loader's own
   pre-install and post-install instructions; no loader binary is bundled.
2. Power the OpenLog/Model 100 with DSR inactive. A missing card is retried once
   per second; no LED or console is required.
3. Enter `RUN "COM:98N1E"` on the Model 100 (or the command required by the chosen
   loader). The manually wired DSR input must become active at this point.
4. The OpenLog sends LOADER.DO at 19,200 baud, paced at 8 ms per byte, followed
   by BASIC EOF (`0x1A`). A card inserted while DSR is active also permits this
   initial bootstrap once the card mounts.
5. After sending, the file closes and TPDD service continues on the same UART.
   There is no MCU reset or UART shutdown. Follow the loader's directions to
   save/run the installed disk software.

Bootstrap sends once per DSR assertion while no file transfer is open. Deassert
and reassert DSR to request another transfer. Valid TPDD traffic consumes the
current bootstrap request to avoid sending a loader in the middle of normal
operation. A missing LOADER.DO sends nothing; reassert DSR after supplying it.
The installation must deliver the intended DSR level/polarity for both loader
and normal use.

## Files and card exchange

- Supported operations: reference/list, open, read, write, append, close, delete
  and rename. The TS-DOS root-label handshake remains for compatibility but
  never enables directory navigation. Format is a harmless success response;
  the device never reformats a PC's card.
- FAT long filenames remain enabled. Names that fit TPDD's 24-byte field and
  base-name padding are returned intact, including internal spaces. Longer,
  non-ASCII, or otherwise unrepresentable names are presented by their FAT
  short alias instead of a truncated name that could select the wrong file.
  Individual Model 100 clients can impose tighter limits; for dependable
  interchange prefer their native six-character base plus `.DO`, `.BA`, or
  `.CO`. Long-name support does not expand a client's own filename limits.
- Subdirectories and hidden files are skipped during listing. File operations
  reject paths and directory pseudo-names; all access stays in the root.
- Read replies contain up to 128 bytes. Incoming payloads retain their complete
  255-byte range. Packet checksums and command lengths are checked; incomplete
  packets time out after 500 ms.
- Each successful write reply follows a data/metadata sync. **Finish the
  operation and close the file before removing the card.** No firmware can make
  removal during an SD write safe. Avoid removing the card during bootstrap.
- After the PC has finished writing, safely eject it from the PC, insert it in
  OpenLog, and request a **fresh directory listing** in the Model 100 client.
  The first-list request remounts the card and clears stale references. An idle
  status request with no selected file also refreshes it. There is no card
  switch: removal is not detected immediately, and an active file must never
  span a card exchange. I/O failures invalidate the mount and subsequent
  operations retry initialization.
- Files larger than 65,535 bytes have their advertised TPDD size saturated at
  65,535; Model 100 RAM/client limits still apply. This is not a full TPDD2
  sector/format/seek implementation.

## Memory and validation

The root-only implementation uses one plain `FatFile`, SdFat's internal root
handle, one selected-name buffer and a shared 256-byte packet/read buffer. It
removes the unused file object, separate 128-byte read buffer, duplicate path
and name buffers, recursive directory scanning and mutable DME state.
Bootstrap reuses the same file handle rather than putting another on the stack.
The validated Uno-profile build uses **15,196 bytes flash and 1,138 bytes static
RAM**, leaving **910 bytes** for the stack and other runtime needs. This includes
bootstrap and long-name support; actual stack headroom needs hardware measurement.

Run host regression tests with a C++17 compiler and Address/UndefinedBehavior
Sanitizers:

```sh
./tests/run.sh
```

The tests compile the actual firmware tabs with fake UART/GPIO and a
filesystem-backed SD adapter. They cover PC/Model 100 file round trips,
maximum-length binary packets and names, long-name aliases, rename/delete,
root-only listing, malformed packets, bootstrap, failed writes/reads, and card
refresh. They validate firmware logic, not SD electrical behavior or timing.

An AVR build and host tests are required before pushing changes. Physical
OpenLog/Model 100 testing is still required: bootstrap with the selected loader,
TS-DOS transfers in both directions, DSR transitions, card reinsertion, power
behavior, and stack headroom during long-name SD operations. No hardware has
been flashed or tested in the cloud environment.
