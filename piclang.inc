;piclang
;Author David Coss
;Version 0.1 (Written 2010)
;License: GNU General Public License 3.0 (see http://www.gnu.org)
;
;This file is part of a package that provides EEPROM IO code, 
;Program management and assembly language to the 
;16F870 PIC microcontroller.
;
;Requires a stack, which stackPtr storing the address of the current
;	location of the stack.
;Requires instruction register.
;Requires exchange register.

#define EOP 0xff;EOP byte

;PICLANG commands
	
PICLANG_COMMAND_TABLE macro
	goto lda		;0x0
	goto adda		;0x1
	goto suba
	goto movaf		
	goto pusha
	goto popa		
	goto anda		
	goto ora		
	goto xora
	goto rra
	goto rla
	goto inca
	goto deca
	goto bsa
	goto bca
	goto clra
	goto goto_piclang
	goto display_piclang
	endm

PICLANG_COMMAND_SET_MAC macro GET_ARG,POP_STACK,PUSH_STACK,accumulator,exchange,instruction,END_OF_FUNCTION,WRITE_EEPROM,leftDisplay,rightDisplay
;Generic set subroutine. Can set current time, alarm time or date.
;Input: Stack values from top to bottom: most significant value, 
;			least significant value.
;Output: null
lda call GET_ARG
	call POP_STACK
	movwf accumulator
	goto END_OF_FUNCTION
adda call GET_ARG
	call POP_STACK
	addwf accumulator,F
	goto END_OF_FUNCTION
suba call GET_ARG
	call POP_STACK
	subwf accumulator,F
	goto END_OF_FUNCTION
SWAP_STACK_X call GET_ARG
	call POP_STACK
	movwf exchange
	goto END_OF_FUNCTION
movaf	movf accumulator,W
	call PUSH_STACK
	call GET_ARG;places F on the stack
	call WRITE_EEPROM
	goto END_OF_FUNCTION
anda call GET_ARG
	call POP_STACK
	andwf accumulator,F
	goto END_OF_FUNCTION
ora call GET_ARG
	call POP_STACK
	iorwf accumulator,F
	goto END_OF_FUNCTION
xora call GET_ARG
	call POP_STACK
	xorwf accumulator,F
	goto END_OF_FUNCTION
pusha movf accumulator,W
	call PUSH_STACK
	goto END_OF_FUNCTION
popa call POP_STACK
     movwf accumulator
     goto END_OF_FUNCTION
goto_piclang call GET_ARG
	call POP_STACK
	movwf programCounter
	goto END_OF_FUNCTION
;Shifts accumulator to the right. Any amount carried over
;goes into exchange, as if exchange is to the right of
;accumulator
; 
;INPUT: The Amount of times shift is performed is on top
;	of the stack
rra	call GET_ARG
	call POP_STACK
	movwf instruction
	movlw 0xff
	andwf instruction,F
	btfsc STATUS,Z
	return			;in case we are told to rotate zero times :-/
	clrf exchange
	rrf accumulator,F
	btfss STATUS,C
	goto $+3
	rrf exchange,F
	bsf exchange,7
	decfsz instruction,F
	goto $-6
	goto END_OF_FUNCTION
	;
;Shifts accumulator to the left. Any amount carried over
;goes into exchange, as if exchange is to the left of
;accumulator
; 
;INPUT: The Amount of times shift is performed is on top
;	of the stack
rla	call GET_ARG
	call POP_STACK
	movwf instruction
	movlw 0xff
	andwf instruction,F
	btfsc STATUS,Z
	return			;in case we are told to rotate zero times :-/
	clrf exchange
	rlf accumulator,F
	btfss STATUS,C
	goto $+3
	rlf exchange,F
	bsf exchange,0
	decfsz instruction,F
	goto $-6
	goto END_OF_FUNCTION
	;
inca incf accumulator,F
	goto END_OF_FUNCTION
	;
deca decf accumulator,F
	goto END_OF_FUNCTION
	;
bsa call bsa_bca_setup
	iorwf accumulator,F
	goto END_OF_FUNCTION
	;
bca call bsa_bca_setup
	movwf tmp
	movlw 0xff
	xorwf tmp,W
	andwf accumulator,F
	goto END_OF_FUNCTION
	;
bsa_bca_setup call GET_ARG
	call POP_STACK
	movwf tmp
	incf tmp,F;1-indexed makes decfsz work
	clrf exchange
	bsf exchange,0
	decfsz tmp,F	
	rlf exchange,F
	movf exchange,W
	return
	;
clra clrf accumulator
	goto END_OF_FUNCTION
display_piclang call POP_STACK
	movwf leftDisplay
	call PUSH_STACK
	movwf rightDisplay
	endm;PICLANG_COMMAND_SET_MAC

;Reads data from EEPROM.
;Uses FSR
;Input: Top of stack will contain address from which to read eeprom data
;Output: Data from eeprom will be at the top of the stack.
;bank 0 is used after return
READ_EEPROM_MAC macro stackPtr
READ_EEPROM bcf STATUS,RP0;bank 0
	bcf STATUS,RP1
	movf stackPtr,W
	movwf FSR
	movf INDF,W
	bsf STATUS,RP1;bank 2
	bcf STATUS,RP0
	movwf EEADR
	bsf STATUS,RP0;bank 3
	bcf EECON1,EEPGD
	bsf EECON1,RD
	bcf STATUS,RP0;bank 2
	movf EEDATA,W
	bcf STATUS,RP1;bank 0
	movwf INDF
	return
	endm
	
;Writes data to EEPROM
;Uses FSR
;Input: EEADR is on the top of the stack and EEDATA is below it.
;Output: EEDATA is on the top of the stack.
;bank 0 is used after return
WRITE_EEPROM_MAC macro stackPtr 
WRITE_EEPROM bcf STATUS,RP0;bank 0
	bcf STATUS,RP1
	movf stackPtr,W
	movwf FSR
	movf INDF,W
	bsf STATUS,RP1;bank 3
	bsf STATUS,RP0
	btfsc EECON1,WR;wait for the last write to finish
	goto $-1
	bcf STATUS,RP0;bank 0
	bcf STATUS,RP1
	movf stackPtr,W
	movwf FSR
	decf stackPtr,F
	movf INDF,W
	bsf STATUS,RP1;bank 2
	movwf EEADR
	bcf STATUS,RP1;bank 0
	movf stackPtr,W
	movwf FSR
	movf INDF,W
	bsf STATUS,RP1;bank 2
	movwf EEDATA
	bsf STATUS,RP0;bank 3
	bcf EECON1,EEPGD;use eeprom, not program flash
	bsf EECON1,WREN;enable write
	BCF INTCON,GIE;temporarily disable interrupts
	movlw 0x55
	movwf EECON2
	movlw 0xaa
	movwf EECON2
	bsf EECON1,WR;start writting
	bsf INTCON,GIE;re-enable interrupts
	bcf EECON1,WR
	bcf EECON1,WREN;disable write.
	bcf STATUS,RP0;bank 0
	bcf STATUS,RP1
	return
	endm
	
;programs EEPROM.
;Last EEADR used is on the top of the stack.
PROGRAM_LOOP_MAC macro dipControl,controlPort,INPUT_BIT,instruction,RUN_PROGRAM,accumulator,exchange,MAIN_LOOP,WRITE_EEPROM,READ_EEPROM,PUSH_STACK,POP_STACK
PROGRAM_LOOP btfss dipControl,INPUT_BIT
	goto $-1
	movf dipControl,W
	movwf instruction
	bcf instruction,INPUT_BIT
	movlw 0x0		;0x0 = run program
	addwf instruction,W
	btfss STATUS,Z
	goto PARSE_INSTRUCTION
	INIT_STACK_MAC stackHead,stackPtr;clear the stack before beginning the program
	movlw 0x0;first EEPROM address of program.
	call PUSH_STACK
	goto RUN_PROGRAM
PARSE_INSTRUCTION btfss instruction,0	;xxxxxx1 = write A to x in EEPROM 
	goto WRITE_A		;x will also be placed at the top of the stack.
	rrf instruction,F
	movf instruction,W
	call PUSH_STACK;this call is to keep the address, x, on the stack after this routine is finished.
	movf accumulator,W
	call PUSH_STACK
	movf instruction,W
	call PUSH_STACK
	call WRITE_EEPROM 	;end of write A to EEPROM
	movf instruction,W
	call PUSH_STACK
	goto LEAVE_STAY_PROGRAM_LOOP
WRITE_A btfss instruction,2	;xxxx1?0  = place x on upper or lower nibble of A
	goto WRITE_EOP;xxxx010 = end of program
	movlw 0xf
	movwf exchange
	btfss instruction,1;exchange will clear parts of A. If 110 clear upper nible. If 100 clear lower.
	swapf exchange,F
	movf exchange,F
	andwf accumulator,W
	call PUSH_STACK
	movf instruction,W
	movwf accumulator;store data to be written in A
	rrf accumulator,F;get rid of 3 instruction bytes
	rrf accumulator,F
	rrf accumulator,F
	btfsc instruction,1
	swapf accumulator,F;swap if we are writting to upper nibble
	call POP_STACK;retrieve old value
	addwf accumulator,F
LEAVE_STAY_PROGRAM_LOOP btfsc controlPort,PROGRAM_MODE_BIT
	goto PROGRAM_LOOP
	goto MAIN_LOOP ; repeat....
	;
;Writes the end of program code(1-byte), starting at address at the top
;of the stack.
WRITE_EOP call POP_STACK
	movwf instruction
	movlw EOP
	call PUSH_STACK
	movf instruction,W
	call PUSH_STACK
	call WRITE_EEPROM
	goto PROGRAM_MAIN_FORK
	endm;PROGRAM_LOOP_MAC

RUN_PROGRAM_MAC macro stackPtr,POP_STACK,PROGRAM_MAIN_FORK,programCounter,READ_EEPROM,instruction,EOP,RUN_COMMAND
RUN_PROGRAM call POP_STACK
	movwf programCounter
RUN_PROGRAM_LOOP movf programCounter,W
	call READ_EEPROM
	movwf instruction
	movlw EOP
	xorwf instruction,W
	btfsc STATUS,Z
	goto PROGRAM_MAIN_FORK
	call RUN_COMMAND;requires piclang.asm
	goto RUN_PROGRAM_LOOP
	endm
	
GET_ARG_MAC macro programCounter,READ_EEPROM,PUSH_STACK
GET_ARG incf programCounter,F
	movf programCounter,W
	call READ_EEPROM
	goto PUSH_STACK
	endm
	
;Runs the command corresponding to the value of "instruction".
;The return value is place at the top of the stack (i.e. stack)
;SUPPLY RUN_COMMAND_TABLE or suffer the consequences!!!
RUN_COMMAND_MAC macro instruction,programCounter,RUN_COMMAND_TABLE
RUN_COMMAND movlw NUM_INSTRUCTIONS
	local tableLOW
	subwf instruction,W
	btfsc STATUS,DC
	goto $+3
	incf instruction,F
	return
	movlw HIGH RUN_COMMAND_TABLE
	movwf PCLATH
	movlw low RUN_COMMAND_TABLE
	addwf instruction,W
	addwf PCL,F;USES RUN_COMMAND_TABLE as a lookup table.
END_OF_FUNCTION incf programCounter,F
	return
	endm;RUN_COMMAND_MAC
