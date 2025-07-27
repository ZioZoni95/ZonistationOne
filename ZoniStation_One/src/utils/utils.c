#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zonistation_common.h"

// Utility functions
void* zs_malloc(zs_size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        ZS_LOG_ERROR("Memory allocation failed: %zu bytes", size);
    }
    return ptr;
}

void* zs_calloc(zs_size_t count, zs_size_t size) {
    void* ptr = calloc(count, size);
    if (ptr == NULL) {
        ZS_LOG_ERROR("Memory allocation failed: %zu * %zu bytes", count, size);
    }
    return ptr;
}

void* zs_realloc(void* ptr, zs_size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        ZS_LOG_ERROR("Memory reallocation failed: %zu bytes", size);
    }
    return new_ptr;
}

void zs_free(void* ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

// String utilities
char* zs_strdup(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    
    zs_size_t len = strlen(str) + 1;
    char* dup = (char*)zs_malloc(len);
    if (dup != NULL) {
        memcpy(dup, str, len);
    }
    return dup;
}

// File utilities
zs_bool zs_file_exists(const char* filename) {
    if (filename == NULL) {
        return ZS_FALSE;
    }
    
    return access(filename, F_OK) == 0;
}

zs_size_t zs_file_size(const char* filename) {
    if (filename == NULL) {
        return 0;
    }
    
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    zs_size_t size = ftell(file);
    fclose(file);
    
    return size;
} 