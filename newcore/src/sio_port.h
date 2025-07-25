// sio_port.h
// Migrated from sio.c: serial I/O logic (header)
// TODO: Move serial I/O declarations here.

#ifndef SIO_PORT_H
#define SIO_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- SIO Port State ---
typedef struct {
    // TODO: Add SIO state, buffers, etc.
} SioPort;

// --- SIO Port API ---
void sio_port_init(SioPort* sio);
void sio_port_send(SioPort* sio, const uint8_t* data, size_t size);
void sio_port_receive(SioPort* sio, uint8_t* buffer, size_t size);

#endif // SIO_PORT_H 