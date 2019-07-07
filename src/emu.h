/* 
 * File:   emu.h
 * Author: dhruvwarrier
 *
 * Starts up the emulator and handles boot.
 * 
 * Created on June 30, 2019, 5:28 PM
 */

#ifndef EMU_H
#define EMU_H

#include <stdbool.h>
#include <stdlib.h>
#include "emu_types.h"
#include "i8080/i8080.h"

#define DEFAULT_START_OF_PROGRAM_MEMORY 0x40

// Allocates the largest amount of memory addressable (in this case 64KB)
bool memory_init(mem_t * const memory_handle);
// Loads a file into memory. Call only after memory_init(). Returns number of words read.
size_t memory_load(const char * file_loc, word_t * memory, addr_t start_loc);

// Create a default interrupt vector table at the top 64 bytes of memory.
// Returns the address from which it is safe to load the program without overwriting the IVT.
addr_t memory_setup_IVT(mem_t * const memory_handle);
// Write a bootloader to the RESET/RST 0 interrupt sequence.
// This must be no more than 8 bytes.
// Returns false if the bootloader is too large.
bool memory_write_bootloader(mem_t * const memory_handle, const word_t * bootloader, size_t bootloader_size);
// Writes a bootloader that simply jumps to the DEFAULT_START_OF_PROGRAM_MEMORY.
void memory_write_bootloader_default(mem_t * const memory_handle);

// Initialize an i8080
void emu_init_i8080(i8080 * const cpu);
// Setup i8080 memory and I/O streams. Returns true if memory stream functions are valid. 
// I/O read/write are allowed to be NULL, but the emulator will crash if an I/O request is made.
bool emu_setup_streams(i8080 * const cpu, read_word_fp read_memory, write_word_fp write_memory, 
        read_word_fp io_port_in, write_word_fp io_port_out);

// Begin the emulator. Must have properly set up memory and streams first!
bool emu_runtime(i8080 * const cpu, mem_t * const memory_handle);

// Cleans up memory
void emu_cleanup(i8080 * cpu, mem_t * memory_handle);

#endif /* EMU_H */
