#include "../include/emulator.h"
#include <string.h>
 
int main(void) {
    EmulatorContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    return emulator_run(&ctx);
} 