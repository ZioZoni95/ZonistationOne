// cdrom_drive.h
// Migrated from cdrom.c: CDROM subsystem logic (header)
// TODO: Move CDROM subsystem declarations here.

#ifndef CDROM_DRIVE_H
#define CDROM_DRIVE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- CDROM Drive State ---
typedef struct {
    // TODO: Add CDROM state, buffers, etc.
} CdromDrive;

// --- CDROM Drive API ---
void cdrom_drive_init(CdromDrive* cd);
void cdrom_drive_process_command(CdromDrive* cd, uint8_t command);
void cdrom_drive_read_data(CdromDrive* cd, uint8_t* buffer, size_t size);

#endif // CDROM_DRIVE_H 