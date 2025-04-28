BOOTSEC = 0x7c0

.code16
.text

.global _start
_start:
    jmpl $BOOTSEC , $start2

start2:
    movw $BOOTSEC , %ax
    movw %ax , %ds
    movw %ax , %es
    movw %ax , %fs
    movw %ax , %gs

    movw $msg , %ax
    movw %ax , %bp
    movw $0x01301 , %ax
    movw $0x0c , %bx # 红色字体
    movw $12 , %cx   # 字符串长度
    movb $0 , %dl
    int $0x010

loop:
    jmp loop

msg:
    .ascii "Hello World!"

.org 510
boot_flag:
    .word 0xaa55
