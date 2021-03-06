;picos stack code
;Author: David Coss
;Date: 10/1/10
;License: GPL v3.0 (http://www.gnu.org)
#define STACK_HEAD_ADDR low stackHead

INIT_STACK_MAC macro headaddr,stackPtr 
	movlw low headaddr;initialize stack
	movwf stackPtr
	endm

;pops the stack onto W-reg
STACK_MAC macro stackTemp,stackTemp2,stackPtr
POP_STACK movf FSR,W ;save the FSR value before popping
	movwf stackTemp
	movf stackPtr,W
	movwf FSR
	movf INDF,W ; get value at top of stack
	movwf stackTemp2 ; temporarily store the popped value
	movf stackTemp,W ; restore previous FSR
	movwf FSR
	movf stackTemp2,W ; popped value is now in w-reg
	decf stackPtr,F
	return

;Returns with the value at the top of the stack in w-reg.
;Unlike pop, peek leaves the data in the stack.
PEEK_STACK call POP_STACK
	incf stackPtr,F
	return

	;
;push W-reg onto the stack
PUSH_STACK movwf stackTemp 
	movf FSR,W
	movwf stackTemp2
	incf stackPtr,F
	movf stackPtr,W
	movwf FSR
	movf stackTemp,W
	movwf INDF
	movf stackTemp2,W
	movwf FSR
	movf stackTemp,W
	return
	
;swap w-reg with top of stack
SWAP_STACK movwf stackTemp ;to top of stack
	movf FSR,W
	movwf stackTemp2 ; save old FSR
	movf stackPtr,W
	movwf FSR
	movf INDF,W
	movwf stackPtr ; borrow stackPtr to store the stack's value
	movf stackTemp,W
	movwf INDF
	movf stackPtr,W
	movwf stackTemp
	movf FSR,W
	movwf stackPtr
	movf stackTemp2,W
	movwf FSR
	movf stackTemp,W
	return
	endm
