#include "cdrom.h"
#include <stddef.h>
 
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) { (void)cdrom; (void)inter; }
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) { (void)cdrom; (void)addr; return 0; }
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) { (void)cdrom; (void)addr; (void)value; }
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename) { (void)cdrom; (void)bin_filename; return false; }
void cdrom_step(Cdrom* cdrom, uint32_t cycles) { (void)cdrom; (void)cycles; } 