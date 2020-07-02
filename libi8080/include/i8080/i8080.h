/*
 * Emulate an i8080.
 *
 * - All instructions supported - documented and undocumented.
 * - Has no dependencies.
 */

#if defined(_MSC_VER) && (_MSC_VER > 1000)
#pragma once
#endif

#ifndef I8080_H
#define I8080_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I8080_WORD = at least 8 bits
 * I8080_DBL_WORD = at least 16 bits
 * MSVC doesn't consistently support chars beyond version 1300
 * so fall back on Microsoft types instead.
 * https://github.com/libgit2/libgit2/blob/master/include/git2/stdint.h
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1300)
	#define I8080_WORD unsigned __int8
	#define I8080_DBL_WORD unsigned __int16
#else
	#define I8080_WORD unsigned char
	#define I8080_DBL_WORD unsigned int
#endif

typedef I8080_WORD i8080_word_t;
typedef I8080_DBL_WORD i8080_addr_t;
typedef I8080_DBL_WORD i8080_dbl_word_t;

#undef I8080_WORD
#undef I8080_DBL_WORD

struct i8080;

struct i8080_debugger
{
	int is_attached;
	/* Return non-zero to signal an error.
	 * i8080_next()/i8080_exec() will
	 * exit with this error code. */
	int(*on_breakpoint)(struct i8080 *);
};

struct i8080
{
	/* Registers */
	i8080_word_t a, b, c, d, e, h, l;
	/* Stack pointer, program counter */
	i8080_addr_t sp, pc;

	/* Status flags:
	 * sign, zero, auxiliary carry, carry, parity */
	int s, z, acy, cy, p;

	/* Interrupt enable. Set/cleared by IE/DE. */
	int inte;

	/* If in HALT state (i.e. stopped execution).
	 * Set by HLT. Can only be cleared by an external interrupt/RESET. */
	int halt;

	/* Set by i8080_interrupt() to indicate pending
	 * interrupt request. Cleared when serviced. */
	int intr;

	i8080_word_t(*memory_read)(i8080_addr_t mem_addr);
	i8080_word_t(*io_read)(i8080_addr_t port_addr);

	void(*memory_write)(i8080_addr_t mem_addr, i8080_word_t word);
	void(*io_write)(i8080_addr_t port_addr, i8080_word_t word);

	i8080_word_t(*interrupt_read)(void);

	/* Clock cycles since reset */
	unsigned long cycles;

	/* Attach a debug routine to be called when a
	 * breakpoint is hit (mapped to RST 7). */
	struct i8080_debugger debugger;

	/* User data */
	void *udata;
};

/*
 * Resets the CPU.
 * PC is set to 0, CPU comes out of HALT, cycles is reset.
 * No working registers or flags are affected.
 * Equivalent to pulling RESET pin low.
 */
void i8080_reset(struct i8080 *const cpu);

/*
 * Executes the next instruction in memory.
 * If an interrupt is pending, services it.
 * Returns 0 if execution is successful.
 */
int i8080_next(struct i8080 *const cpu);

/*
 * Executes a given opcode.
 * Updates cycle count, registers and flags.
 * Returns 0 if execution is successful.
 */
int i8080_exec(struct i8080 *const cpu, i8080_word_t opcode);

/*
 * Sends an interrupt to the CPU.
 * When ready, i8080 will call interrupt_read() and execute the returned opcode.
 */
void i8080_interrupt(struct i8080 *const cpu);

#ifdef __cplusplus
}
#endif

#endif /* I8080_H */