#include <stdint.h>
#include "gpu.h"
#include "interconnect.h"
void gpu_init_full(Gpu* gpu, Interconnect* inter) { (void)gpu; (void)inter; }
uint32_t gpu_read_data(Gpu* gpu) { (void)gpu; return 0; }
uint32_t gpu_read_status(Gpu* gpu) { (void)gpu; return 0; }
void gpu_gp0(Gpu* gpu, uint32_t command) { (void)gpu; (void)command; }
void gpu_gp1(Gpu* gpu, uint32_t command) { (void)gpu; (void)command; } 