#include "gpu.h"
#include <stddef.h>
 
void gpu_init(Gpu* gpu, Interconnect* inter) { (void)gpu; (void)inter; }
void gpu_gp0(Gpu* gpu, uint32_t value) { (void)gpu; (void)value; }
void gpu_gp1(Gpu* gpu, uint32_t value) { (void)gpu; (void)value; }
uint32_t gpu_read_data(Gpu* gpu) { (void)gpu; return 0; }
uint32_t gpu_read_status(Gpu* gpu) { (void)gpu; return 0; } 