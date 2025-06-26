#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define MAX_LINE 2048
#define MODULE_COUNT 11

typedef struct {
    const char* name;
    const char* keyword;
    FILE* file;
} Module;

int main() {
    // Define modules and their identifying keywords
    Module modules[MODULE_COUNT] = {
        {"cpu",      "CPU"},
        {"gpu",      "GPU"},
        {"cdrom",    "CDROM"},
        {"dma",      "DMA"},
        {"ram",      "RAM"},
        {"vram",     "VRAM"},
        {"renderer", "Renderer"},
        {"bios",     "BIOS"},
        {"debugger", "Debugger"},
        {"interconnect", "INTERCONNECT"},
        {"timers", "TIMER"}
    };

    FILE* misc = fopen("misc_log.txt", "w");
    if (!misc) { perror("misc_log.txt"); return 1; }

    // Open output files for each module
    for (int i = 0; i < MODULE_COUNT; ++i) {
        char fname[64];
        snprintf(fname, sizeof(fname), "%s_log.txt", modules[i].name);
        modules[i].file = fopen(fname, "w");
        if (!modules[i].file) { perror(fname); return 1; }
    }

    FILE* in = fopen("emulator_log.txt", "r");
    if (!in) { perror("emulator_log.txt"); return 1; }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        bool matched = false;
        for (int i = 0; i < MODULE_COUNT; ++i) {
            if (strstr(line, modules[i].keyword)) {
                fputs(line, modules[i].file);
                matched = true;
                break; // Remove this if you want lines in multiple logs
            }
        }
        if (!matched) {
            fputs(line, misc);
        }
    }

    fclose(in);
    fclose(misc);
    for (int i = 0; i < MODULE_COUNT; ++i) {
        fclose(modules[i].file);
    }

    printf("Log splitting complete.\n");
    return 0;
} 