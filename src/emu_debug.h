/* 
 * File:   emu_debug.h
 * Author: dhruvwarrier
 *
 * Contains some functions to help debug.
 * 
 * Created on June 30, 2019, 8:24 PM
 */

#ifndef EMU_DEBUG_H
#define EMU_DEBUG_H

#include "emu_types.h"
#include "i8080/i8080.h"

// Dumps the contents of memory to file
void dump_memory(const word_t * memory, addr_t highest_addr);
// Dumps the values of all registers and flags
void dump_cpu_stats(i8080 * const cpu);

#endif /* EMU_DEBUG_H */