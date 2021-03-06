#define USART_READ_FLAG PIR1,RCIF
#define USART_WRITE_FLAG PIR1,TXIF

#define SSP_STAT_FLAG SSPSTAT, BF

INIT_USART macro usart_addr
	banksel usart_addr
	clrf usart_addr
	banksel ANSELH
	clrf ANSELH
	bcf STATUS,RP1
	bsf STATUS,RP0
	movlw	0xA0 ; set the 
	iorwf	TRISB,F
	bcf OPTION_REG,7
	movlw	.24	;set baud rate
	movwf	SPBRG
	movlw 1 ; 10417
	movwf SPBRGH
	movlw	0x24		;enable transmission and high baud rate
	movwf	TXSTA
	banksel RCSTA
	movlw	0x80		;enable serial port and reception
	movwf	RCSTA

	movlw	0xc0		;enable global and peripheral ints
	iorwf	INTCON,F
	bsf RCSTA,CREN
	banksel PIE1
	bsf PIE1,RCIE ; enable only Receive
	bcf STATUS,RP0			;select bank0
	bcf STATUS,RP0
	endm

USART_ISR_MAC macro NEXT_ISR
	banksel PIR1
	btfss USART_READ_FLAG
	goto CHECK_WRITE_USART
	banksel PIE1
	btfsc PIE1,RCIE
	goto READ_USART
CHECK_WRITE_USART bcf STATUS,RP0
	bcf STATUS,RP1
	btfss USART_WRITE_FLAG
	goto NEXT_ISR
	banksel PIE1
	btfsc PIE1,TXIE
	goto WRITE_USART
	endm

;Code for copying to and from the usart_addr location.
;Does not return. Instead it goest to RETURN_ADDR upon completion.
;This is done to be gental to the call stack during an interrupt.
USART_COPY_MAC macro myStatus,usart_addr,errorByte,PUSH_STACK,POP_STACK,RETURN_ADDR

;Copies the character that is in RCREG to the address stored in usart_addr
;If usart_adder is null, nothing happens.
;Leaves in bank 0
READ_USART movlw 0
	banksel PIE1
	bcf PIE1,RCIE ; clear interrupt during the read process
	banksel RCSTA
	btfsc	RCSTA,OERR	;check overrun error flag
	goto	ERROR_OVRRUN		
	btfsc	RCSTA,FERR	;test framing error flag
	goto	ERROR_FRAME	
	movf FSR,W
	call PUSH_STACK
	banksel RCREG
	movf RCREG,W
	call PUSH_STACK
	banksel usart_addr
	movf usart_addr,W
	btfsc STATUS,Z
	goto READ_USART_IGNORE_ADDR
	movwf FSR
	call POP_STACK
	movwf INDF
	call POP_STACK
	movwf FSR
	goto END_READ_USART
READ_USART_IGNORE_ADDR call POP_STACK
	goto END_READ_USART
ERROR_OVRRUN bcf RCSTA,CREN
	bsf RCSTA,CREN
	movlw USART_ERROR
	movwf errorByte
	goto END_READ_USART
ERROR_FRAME	movf RCREG,W
	movlw USART_ERROR
	movwf errorByte
END_READ_USART nop
	banksel PIE1
	bsf PIE1,RCIE ; re-enable rx interrupt
	bcf STATUS,RP0
	bcf STATUS,RP1
	goto RETURN_ADDR
;Copies the data in the address stored in usart_addr to TXREG.
;If usart_addr is null, nothing happens.
;Leaves in bank 0
WRITE_USART movlw 0
	xorwf usart_addr,W
	btfsc STATUS,Z
	goto END_WRITE_USART
	movf FSR,W
	call PUSH_STACK
	banksel usart_addr
	movf usart_addr,W
	movwf FSR
	movf INDF,W
	banksel TXREG
	movwf TXREG
	call POP_STACK
	movwf FSR
END_WRITE_USART nop
	banksel PIE1
	bcf PIE1,TXIE
	bcf STATUS,RP0
	bcf STATUS,RP1
	goto RETURN_ADDR
	
	endm
	
;Copies the SSPBUF to the desired memory location and then copies the next
;transmit location to SSPBUF, in effect syncronizing the read/write process
;between user mode processes and the SSP process.
;
;Does not return. Instead goes to RETURN_ADDR after completion.
;Returns in bank 0
SYNC_SSP_MAC macro rx_addr,tx_addr,PUSH_STACK,POP_STACK,RETURN_ADDR
SYNC_SSP movf FSR,W
	call PUSH_STACK
	banksel rx_addr
	movlw 0
	xorwf rx_addr,W
	btfsc STATUS,Z
	goto SYNC_TRANSMIT_SSP
	movwf FSR
	banksel SSPBUF
	movf SSPBUF,W
	movwf INDF
SYNC_TRANSMIT_SSP movlw 0
	banksel tx_addr
	xorwf tx_addr,W
	btfsc STATUS,Z
	goto END_SYNC_SSP
	movwf FSR
	banksel SSPBUF
	movf INDF,W
	movwf SSPBUF
	bcf myStatus,SSP_WRITE_BUSY_FLAG
END_SYNC_SSP goto RETURN_ADDR
	endm