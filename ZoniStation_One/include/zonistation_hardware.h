#ifndef ZONISTATION_HARDWARE_H
#define ZONISTATION_HARDWARE_H

#include "zonistation_common.h"
#include "zonistation_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct zs_gpu_t zs_gpu_t;
typedef struct zs_spu_t zs_spu_t;
typedef struct zs_cdrom_t zs_cdrom_t;

// Hardware initialization and shutdown
zs_error_t zs_hardware_init(zs_gpu_t** gpu, zs_spu_t** spu, zs_cdrom_t** cdrom, const zs_config_t* config);
zs_error_t zs_hardware_shutdown(zs_gpu_t* gpu, zs_spu_t* spu, zs_cdrom_t* cdrom);
zs_error_t zs_hardware_reset(zs_gpu_t* gpu, zs_spu_t* spu, zs_cdrom_t* cdrom);
zs_error_t zs_hardware_update(zs_gpu_t* gpu, zs_spu_t* spu, zs_cdrom_t* cdrom, zs_u32 cycles);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_HARDWARE_H 