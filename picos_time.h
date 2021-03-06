/**
 * PICOS, PIC operating system.
 * Author: David Coss, PhD
 * Date: 1 Sept 2011
 * License: GNU Public License version 3.0 (see http://www.gnu.org)
 *
 * This file prototypes time functions.
 */
#ifndef PICOS_TIME_H
#define PICOS_TIME_H 1

#define TIME_PERIOD 25

typedef unsigned char quantum_t;
volatile quantum_t TIME_tickCounter;

typedef struct {
  char hours;
  char minutes;
  char seconds;
  char month;
  char day;
  char year; // since 2000
}TIME_t;

/**
 * Initializes time structure.
 */
extern void TIME_init(void);

/**
 * Increments time one second.
 */
extern void TIME_tick(void);
extern void TIME_inc_minute();

extern void TIME_sleep(char x);

/**
 * Sets time
 */
extern void TIME_set(TIME_t *t);

/**
 * Retrieves a pointer to the current time structure.
 */
extern const TIME_t* TIME_get();

#endif

