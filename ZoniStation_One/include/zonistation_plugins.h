#ifndef ZONISTATION_PLUGINS_H
#define ZONISTATION_PLUGINS_H

#include "zonistation_common.h"
#include "zonistation_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Plugin types
typedef enum {
    ZS_PLUGIN_TYPE_GPU = 0,
    ZS_PLUGIN_TYPE_SPU,
    ZS_PLUGIN_TYPE_INPUT,
    ZS_PLUGIN_TYPE_CDROM,
    ZS_PLUGIN_TYPE_COUNT
} zs_plugin_type_t;

// Plugin structure
typedef struct zs_plugin_t {
    zs_plugin_type_t type;
    char name[64];
    char version[32];
    void* handle;
    void* data;
    zs_bool loaded;
} zs_plugin_t;

// Plugin manager structure
typedef struct zs_plugin_manager_t {
    zs_plugin_t plugins[ZS_PLUGIN_TYPE_COUNT];
    const zs_config_t* config;
    zs_bool initialized;
} zs_plugin_manager_t;

// Plugin manager functions
zs_error_t zs_plugin_manager_init(zs_plugin_manager_t** manager, const zs_config_t* config);
zs_error_t zs_plugin_manager_shutdown(zs_plugin_manager_t* manager);

// Plugin loading and management
zs_error_t zs_plugin_manager_load_plugin(zs_plugin_manager_t* manager, zs_plugin_type_t type, const char* path);
zs_error_t zs_plugin_manager_unload_plugin(zs_plugin_manager_t* manager, zs_plugin_type_t type);
zs_plugin_t* zs_plugin_manager_get_plugin(zs_plugin_manager_t* manager, zs_plugin_type_t type);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_PLUGINS_H 