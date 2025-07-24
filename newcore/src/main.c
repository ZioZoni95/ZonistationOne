#include "../include/emulator.h"
#include <string.h>
#include "../include/log.h"
 
int main(void) {
    nc_log_set_level(NC_LOG_INFO); // Show status/info messages, but hide debug/trace
    EmulatorContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    return emulator_run(&ctx);
} 