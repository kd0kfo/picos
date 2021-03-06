/**
 * PICOS, PIC operating system.
 * Author: David Coss, PhD
 * Date: 1 Sept 2011
 * License: GNU Public License version 3.0 (see http://www.gnu.org)
 *
 * This function provides a buffered system for handling command arguments.
 */
#ifndef ARG_H
#define ARG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "picos/utils.h"

#ifndef ARG_SIZE
#define ARG_SIZE 17
#endif

enum {ARG_SHELL,ARG_PICLANG};
char ARG_next;// index into ARG_buffer 
char ARG_end;
char ARG_source;// source of arguments, i.e. shell or piclang

#define ARG_EOA() (ARG_next >= ARG_SIZE)

/**
 * Clears argument buffer
 */
void ARG_clear();

/**
 * Places the next integer of the argument buffer into the memory 
 * with the address supplied as a parameter.
 *
 * For an invalid argument, -1 is returned and error_code is set.
 * Upon successful conversion, SUCCESS is returned (see picfs_error.h)
 */
signed char ARG_getd(picos_size_t *val);

void ARG_putch(char ch);
signed char ARG_getch();
const char* ARG_gets();

char ARG_count();
signed char ARG_get(char index);
#endif
