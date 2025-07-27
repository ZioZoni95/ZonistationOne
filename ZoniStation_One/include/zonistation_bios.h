#ifndef ZONISTATION_BIOS_H
#define ZONISTATION_BIOS_H

#include "zonistation_common.h"
#include "zonistation_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// BIOS structure
typedef struct zs_bios_t {
    zs_bool initialized;
    zs_bool hle_enabled;
    zs_bool bios_loaded;
    const zs_config_t* config;
} zs_bios_t;

// BIOS initialization and shutdown
zs_error_t zs_bios_init(zs_bios_t** bios, const zs_config_t* config);
zs_error_t zs_bios_shutdown(zs_bios_t* bios);
zs_error_t zs_bios_reset(zs_bios_t* bios);

// BIOS loading
zs_error_t zs_bios_load_file(zs_bios_t* bios, const char* filename);

// BIOS functions
zs_error_t zs_bios_call_function(zs_bios_t* bios, zs_u32 function_id);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_BIOS_H 