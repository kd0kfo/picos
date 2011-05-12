#ifndef _SERIAL_H_
#define _SERIAL_H_

#define BAUD 9600      
#define FOSC 4000000L
#define NINE 0     /* Use 9bit communication? FALSE=8bit */

#define DIVIDER ((int)(FOSC/(16UL * BAUD) -1))
#define HIGH_SPEED 1

#if NINE == 1
#define NINE_BITS 0x40
#else
#define NINE_BITS 0
#endif

#if HIGH_SPEED == 1
#define SPEED 0x4
#else
#define SPEED 0
#endif

#define USART_have_incoming() RCIF
#define USART_DEFAULT_TIMEOUT 128;
volatile char USART_timeout;

/* Serial initialization */
extern void usart_init();
extern void usart_putch(unsigned char);
extern char usart_getch(void);
extern char usart_getche(void);

/**
 * Continuously streams usart to eeprom
 * Ends after 0xde byte followed by 0xad byte
 */
extern void usart2eeprom(char start_address);

/**
 * Send the size number of bytes from eeprom (starting
 * at addr) to usart
 */
void eeprom2usart(char addr, char size);
#endif
