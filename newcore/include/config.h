#ifndef NC_CONFIG_H
#define NC_CONFIG_H

// @pcs: Emulator configuration structure (stub)
typedef struct NcConfig {
    // TODO: Add config fields (BIOS path, region, etc.)
    int dummy;
} NcConfig;

// @pcs: Load configuration from file (stub)
void config_load(const char* filename);

#endif // NC_CONFIG_H 