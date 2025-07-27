#ifndef ZONISTATION_CDROM_H
#define ZONISTATION_CDROM_H

#include "zonistation_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct zs_cdrom_t zs_cdrom_t;

// CD-ROM functions
zs_error_t zs_cdrom_load_game(zs_cdrom_t* cdrom, const char* game_path);
zs_error_t zs_cdrom_unload_game(zs_cdrom_t* cdrom);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_CDROM_H 