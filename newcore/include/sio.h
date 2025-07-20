#ifndef NEWCORE_SIO_H
#define NEWCORE_SIO_H

#include <stdint.h>
#include <stdbool.h>

// SIO (Serial I/O) state structure for newcore
typedef struct {
    uint8_t status;
    // TODO: Add more SIO state fields as needed
} NcSio;

// Initialize SIO
void nc_sio_init(NcSio* sio);

#endif // NEWCORE_SIO_H 