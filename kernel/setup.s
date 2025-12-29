INITSEG = 0x9000
SYSSEG  = 0x1000
SETUPSEG = 0x9020

.code16
.text

.globl _start_setup
_start_setup:
    movw $INITSEG, %ax
    movw %ax, %ds
    movb $0x03, %ah
    xor  %bh, %bh
    int  $0x10
    movw %dx, (0)
    movb $0x88, %ah
    int  $0x15
    movw %ax, (2)