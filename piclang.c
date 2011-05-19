#include <htc.h>
#include <stdio.h>

#include "defines.h"
#include "page.h"
#include "error.h"
#include "piclang.h"
#include "arg.h"
#include "io.h"
#include "utils.h"

#define PICLANG_error(code)  curr_process.status = code

char PICLANG_load(char nth)
{
  char size = 0,pos = 0,counter = 0;
  
  for(;pos < 0xff;)
    {
      size = eeprom_read(pos);
      if(counter == nth)
	break;
      pos = pos + size;// move to beginning of next PCB
      counter++;
    }

  if(size == 0)
    return PICLANG_NO_SUCH_PROGRAM;

  curr_process.offset = pos;// offset must equal the address of the first byte.
  curr_process.size = size;
  eeprom_write(++pos,curr_process.offset);// update offset
  curr_process.bitmap = eeprom_read(++pos);
  curr_process.num_pages = eeprom_read(++pos);
  curr_process.pc = eeprom_read(++pos);
  curr_process.status = eeprom_read(++pos);
  curr_process.start_address = eeprom_read(++pos);
  curr_process.string_address = eeprom_read(++pos);

  counter = 0;
  for(;counter<PICLANG_STACK_SIZE;counter++)
    curr_process.stack[counter] = eeprom_read(++pos);
  curr_process.stack_head = eeprom_read(++pos);

  PICLANG_quantum = DEFAULT_PICLANG_QUANTUM;

  if(curr_process.status != PICLANG_SUSPENDED)
    {
      char retval = PAGE_request(curr_process.num_pages,curr_process.start_address);
      if(retval != 0)
	return error_code;
    }

  if((curr_process.bitmap & PICLANG_BIT_SYSCALL) != 0)
    ARG_source = ARG_PICLANG;

  return PICLANG_SUCCESS;

}

char PICLANG_save(char saved_status)
{
  char pos,status;
  if(curr_process.size == 0)
    return PICLANG_NO_SUCH_PROGRAM;

  pos = curr_process.start_address;
  if(pos - 5 - PICLANG_STACK_SIZE < 0)
    return PICLANG_EEPROM_OVERFLOW;
  
  pos--;// move back to stack head
  eeprom_write(pos--,curr_process.stack_head);
  status = PICLANG_STACK_SIZE - 1;
  for(;;status--)// save the stack
    {    
      eeprom_write(pos--,curr_process.stack[status]);
      if(status == 0)
	break;
    }
  pos--;// skip start_address
  curr_process.status = saved_status;
  status = curr_process.status;
  eeprom_write(pos,curr_process.status);
  pos--;
  eeprom_write(pos,curr_process.pc);
  

  PICLANG_init();

  return status;

}


void PICLANG_init()
{
  curr_process.size = 0;
  curr_process.offset = 0;
  curr_process.pc = 0;
  curr_process.status = PICLANG_SUSPENDED;
  curr_process.start_address = 0;
  curr_process.string_address = 0;
  curr_process.stack_head = 0;
  PICLANG_quantum = 0;
}

char PICLANG_get_next_byte(){
  char next = curr_process.pc++;
  if((next + PCB_SIZE) > curr_process.size)
    {
      PICLANG_error(PICLANG_PC_OVERFLOW);
      return EOP;
    }
  next += curr_process.start_address + curr_process.offset;
  return eeprom_read(next);
}

void PICLANG_pushl(char val)
{
  curr_process.stack_head++;
  if(curr_process.stack_head > PICLANG_STACK_SIZE)
    {
      PICLANG_error(PICLANG_STACK_OVERFLOW);
      return;
    }
  curr_process.stack[curr_process.stack_head] = val;
}

char PICLANG_pop()
{
  if(curr_process.stack_head > PICLANG_STACK_SIZE)
    {
      PICLANG_error(PICLANG_STACK_OVERFLOW);
      return 0;
    }
  return curr_process.stack[curr_process.stack_head--];
}

void PICLANG_next()
{
  char command;
  if(curr_process.size == 0)
    return;

  if(PICLANG_quantum == 0)
    {
      PICLANG_save(PICLANG_SUSPENDED);
      return;
    }
  
  command = PICLANG_get_next_byte();
  switch(command)
    {
    case EOP:
      PICLANG_save(SUCCESS);
      return;
    case PICLANG_ADD:
      PICLANG_pushl(PICLANG_pop() + PICLANG_pop());
      return;
    case PICLANG_SUB:
      PICLANG_pushl(PICLANG_pop() - PICLANG_pop());
      return;
    case PICLANG_MULT:
      PICLANG_pushl(PICLANG_pop() * PICLANG_pop());
      return;
    case PICLANG_PUSHL:
      PICLANG_pushl(PICLANG_get_next_byte());
      break;
    case PICLANG_PUSH:
      {
	char val = PAGE_get(PICLANG_get_next_byte(),curr_process.offset);
	if(error_code != SUCCESS)
	  PICLANG_error(error_code);
	else
	  PICLANG_pushl(val);
	break;
      }
    case PICLANG_POP:
      {
	char addr = PICLANG_get_next_byte();
	PAGE_set(addr,PICLANG_pop(),curr_process.offset);
	break;
      }
    case PICLANG_PRINT:
      {
	putch(PICLANG_pop());
	IO_flush();
	break;
      }
    case PICLANG_PRINTL:
      IO_putd(PICLANG_pop());
      IO_flush();
      break;
    case PICLANG_SPRINT:
      {
	char addr = PICLANG_pop(),ch;
	static bit should_flush;
	addr += curr_process.offset + curr_process.string_address;
	ch = eeprom_read(addr++);
	if(ch == 0)
	  should_flush = FALSE;
	else
	  should_flush = TRUE;
	while(ch != 0)
	  {
	    putch(ch);
	    ch = eeprom_read(addr++);
	  }
	if(should_flush == TRUE)
	  IO_flush();
	break;
      }
    case PICLANG_SYSTEM:
      PICLANG_system = PICLANG_pop();
      PICLANG_quantum = 0;// will suspend for system call
      break;
    case PICLANG_NUM_COMMANDS:default:
      PICLANG_error(PICLANG_UNKNOWN_COMMAND);
      return;
    }
}
