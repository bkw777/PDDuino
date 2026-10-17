/*
 * PDDuino OpenLog - internal Model 100 TPDD drive.
 * Derived from PDDuino by bkw777 and SD2TPDD by Jimmy Pettit.
 * See LICENSE (GPL-3.0).
 */
#include <SPI.h>
#include <SdFat.h>
#include "board_OpenLog.h"

#if !defined(PDDUINO_HOST_TEST) && (!defined(__AVR_ATmega328P__) || F_CPU != 16000000UL)
#error "Select ATmega328P at 16 MHz (Arduino Uno compile profile)"
#endif
#if !USE_LONG_FILE_NAMES
#error "PDDuino requires SdFat long filename support"
#endif

// Preserve the complete one-byte-length TPDD payload. Reads reuse this buffer.
const uint16_t DATA_BUFFER_SZ = 256;
const uint8_t READ_BLOCK_SZ = 128;
const uint8_t WIRE_NAME_SZ = 24;
const uint8_t NAME_SZ = WIRE_NAME_SZ + 1;

// TPDD commands
#define CMD_REFERENCE     0x00
#define CMD_OPEN          0x01
#define CMD_CLOSE         0x02
#define CMD_READ          0x03
#define CMD_WRITE         0x04
#define CMD_DELETE        0x05
#define CMD_FORMAT        0x06
#define CMD_STATUS        0x07
#define CMD_DMEREQ        0x08
#define CMD_SEEK          0x09
#define CMD_TELL          0x0a
#define CMD_SET_EXT       0x0b
#define CMD_CONDITION     0x0c
#define CMD_RENAME        0x0d
#define CMD_REQ_EXT_QUERY 0x0e
#define CMD_COND_LIST     0x0f
#define CMD_TSDOS_UNK_2   0x23
#define CMD_TSDOS_UNK_1   0x31

// TPDD returns
#define RET_READ          0x10
#define RET_DIRECTORY     0x11
#define RET_NORMAL        0x12
#define RET_CONDITION     0x15

// TPDD errors
#define ERR_SUCCESS       0x00
#define ERR_NO_FILE       0x10
#define ERR_EXISTS        0x11
#define ERR_NO_NAME       0x30 // command.tdd calls this EOF
#define ERR_DIR_SEARCH    0x31
#define ERR_BANK          0x35
#define ERR_PARM          0x36
#define ERR_FMT_MISMATCH  0x37
#define ERR_EOF           0x3f
#define ERR_NO_START      0x40 // command.tdd calls this IO ERROR (64) (empty name error)
#define ERR_ID_CRC        0x41
#define ERR_SEC_LEN       0x42
#define ERR_FMT_VERIFY    0x44
#define ERR_FMT_INTRPT    0x46
#define ERR_ERASE_OFFSET  0x47
#define ERR_DATA_CRC      0x49
#define ERR_SEC_NUM       0x4a
#define ERR_READ_TIMEOUT  0x4b
#define ERR_SEC_NUM2      0x4d
#define ERR_WRITE_PROTECT 0x50 // writing to a locked file
#define ERR_DISK_NOINIT   0x5e
#define ERR_DIR_FULL      0x60 // command.tdd calls this disk full
#define ERR_DISK_FULL     0x61
#define ERR_FILE_LEN      0x6e
#define ERR_NO_DISK       0x70
#define ERR_DISK_CHG      0x71

// SdFat owns the root directory and its single 512-byte sector cache.
// One plain FatFile is shared by file transfers, listing and bootstrap.
SdFat SD;
FatFile entry;
uint8_t packet[DATA_BUFFER_SZ];
char selectedName[NAME_SZ] = "";
uint8_t payloadLength = 0;
uint8_t txChecksum = 0;
uint8_t openMode = 0;
bool mounted = false;
bool bootstrapArmed = true;
unsigned long lastCardAttempt = 0;
uint32_t nextDirectoryPosition = 0;

const uint8_t F_OPEN_WRITE = 1;
const uint8_t F_OPEN_APPEND = 2;
const uint8_t F_OPEN_READ = 3;
const uint8_t SF_NAME = 0;
const uint8_t SF_FIRST = 1;
const uint8_t SF_NEXT = 2;
const uint8_t BASIC_EOF = 0x1A;
