void command_reference() {
  if (payloadLength != 26 || entry.isOpen()) { return_normal(ERR_PARM); return; }
  uint8_t search = packet[25];
  if (search == SF_NAME) {
    if (!decodeName(packet, selectedName)) { selectedName[0] = 0; return_normal(ERR_PARM); return; }
    if (!ensureCard()) { return_normal(ERR_NO_DISK); return; }
    // ensureCard may have remounted and cleared the selection.
    if (!decodeName(packet, selectedName)) { return_normal(ERR_PARM); return; }
    if (!entry.open(selectedName, O_READ)) {
      returnReference(NULL, 0);  // Keep the requested name for a subsequent create.
      return;
    }
    if (!entry.isFile()) {
      entry.close(); selectedName[0] = 0; returnReference(NULL, 0); return;
    }
    uint32_t size = entry.fileSize();
    bool named = selectEntryName();
    entry.close();
    if (!named) { storageError(ERR_NO_DISK); return; }
    returnReference(selectedName, size);
    return;
  }
  if (search != SF_FIRST && search != SF_NEXT) { return_normal(ERR_PARM); return; }
  // A new listing is the explicit card-refresh point after a PC exchange.
  if (search == SF_FIRST ? !mountCard() : !ensureCard()) { return_normal(ERR_NO_DISK); return; }
  selectedName[0] = 0;
  // Name lookups also move SdFat's internal root cursor; restore the listing.
  if (!SD.vwd()->seekSet(nextDirectoryPosition)) { storageError(ERR_NO_DISK); return; }
  while (entry.openNext(SD.vwd(), O_READ)) {
    nextDirectoryPosition = SD.vwd()->curPosition();
    if (!entry.isFile() || entry.isHidden()) { entry.close(); continue; }
    uint32_t size = entry.fileSize();
    bool named = selectEntryName();
    entry.close();
    if (!named) { storageError(ERR_NO_DISK); return; }
    returnReference(selectedName, size);
    return;
  }
  nextDirectoryPosition = SD.vwd()->curPosition();
  if (SD.vwd()->getError()) { storageError(ERR_NO_DISK); return; }
  returnReference(NULL, 0);
}

void command_open() {
  if (payloadLength != 1 || !selectedName[0] || entry.isOpen()) { return_normal(ERR_PARM); return; }
  uint8_t mode = packet[0];
  if (mode < F_OPEN_WRITE || mode > F_OPEN_READ) { return_normal(ERR_PARM); return; }
  if (!ensureCard()) { return_normal(ERR_NO_DISK); return; }
  // A remount invalidates any reference made against the previous card.
  if (!selectedName[0]) { return_normal(ERR_NO_FILE); return; }
  oflag_t flags = mode == F_OPEN_WRITE ? O_WRITE | O_CREAT | O_TRUNC :
                  mode == F_OPEN_APPEND ? O_WRITE | O_APPEND : O_READ;
  if (!entry.open(selectedName, flags)) { return_normal(ERR_NO_FILE); return; }
  if (!entry.isFile()) { entry.close(); return_normal(ERR_PARM); return; }
  openMode = mode;
  return_normal(ERR_SUCCESS);
}

void command_close() {
  if (payloadLength) { return_normal(ERR_PARM); return; }
  bool ok = !entry.isOpen() || entry.sync();
  entry.close();
  openMode = 0;
  if (!ok) { storageError(ERR_NO_DISK); return; }
  return_normal(ERR_SUCCESS);
}

void command_read() {
  if (payloadLength || !entry.isOpen() || openMode != F_OPEN_READ) { return_normal(ERR_PARM); return; }
  int count = entry.read(packet, READ_BLOCK_SZ);
  if (count < 0) { storageError(ERR_READ_TIMEOUT); return; }
  if (!count) { return_normal(ERR_EOF); return; }
  tpddWrite(RET_READ);
  tpddWrite(static_cast<uint8_t>(count));
  for (int i = 0; i < count; ++i) tpddWrite(packet[i]);
  tpddSendChecksum();
}

void command_write() {
  if (!entry.isOpen() || (openMode != F_OPEN_WRITE && openMode != F_OPEN_APPEND)) { return_normal(ERR_PARM); return; }
  // Commit both data and directory metadata before acknowledging each block.
  if (entry.write(packet, payloadLength) != payloadLength || !entry.sync()) {
    storageError(ERR_DISK_FULL); return;
  }
  return_normal(ERR_SUCCESS);
}

void command_delete() {
  if (payloadLength || !selectedName[0] || entry.isOpen()) { return_normal(ERR_PARM); return; }
  if (!ensureCard()) { return_normal(ERR_NO_DISK); return; }
  if (!selectedName[0] || !entry.open(selectedName, O_WRITE)) { return_normal(ERR_NO_FILE); return; }
  if (!entry.isFile()) { entry.close(); return_normal(ERR_PARM); return; }
  bool ok = entry.remove();
  entry.close();
  if (!ok) { storageError(ERR_NO_DISK); return; }
  selectedName[0] = 0;
  return_normal(ERR_SUCCESS);
}

void command_rename() {
  if (payloadLength < WIRE_NAME_SZ || !selectedName[0] || entry.isOpen()) { return_normal(ERR_PARM); return; }
  char* newName = reinterpret_cast<char*>(packet);
  if (!decodeName(packet, newName)) { return_normal(ERR_PARM); return; }
  if (!ensureCard()) { return_normal(ERR_NO_DISK); return; }
  if (!selectedName[0] || !entry.open(selectedName, O_WRITE)) { return_normal(ERR_NO_FILE); return; }
  if (!entry.isFile()) { entry.close(); return_normal(ERR_PARM); return; }
  if (SD.exists(newName)) { entry.close(); return_normal(ERR_EXISTS); return; }
  bool ok = entry.rename(newName);
  entry.close();
  if (!ok) { storageError(ERR_NO_DISK); return; }
  strcpy(selectedName, newName);
  return_normal(ERR_SUCCESS);
}

void command_status() {
  // Refresh an idle card without invalidating an active file transfer.
  if ((entry.isOpen() || selectedName[0]) ? !ensureCard() : !mountCard()) { return_normal(ERR_NO_DISK); return; }
  return_normal(ERR_SUCCESS);
}

// TS-DOS root label handshake is retained, but never enables directories.
void command_DMEreq() {
  tpddWrite(RET_NORMAL);
  tpddWrite(0x0B);
  tpddWrite(' ');
  const char label[] = "SD:   .<> ";
  for (uint8_t i = 0; i < sizeof(label) - 1; ++i) tpddWrite(label[i]);
  tpddSendChecksum();
}

void dispatchCommand(uint8_t command) {
  switch (command) {
    case CMD_REFERENCE: command_reference(); break;
    case CMD_OPEN: command_open(); break;
    case CMD_CLOSE: command_close(); break;
    case CMD_READ: command_read(); break;
    case CMD_WRITE: command_write(); break;
    case CMD_DELETE: command_delete(); break;
    case CMD_RENAME: command_rename(); break;
    case CMD_STATUS:
    case CMD_CONDITION: command_status(); break;
    case CMD_DMEREQ: command_DMEreq(); break;
    // Never format a PC's card. Preserve upstream's harmless success reply.
    case CMD_FORMAT: return_normal(ERR_SUCCESS); break;
    default: return_normal(ERR_PARM); break;
  }
}
