.include "linux32.s"

.section .data

out_fd: # 文件描述符
    .long 1
long_arr:
    .long -11 , 2 , 3
byte_arr:
    .byte 'h' , 'e' , 'l' , 'l' , 'o' , ',' , 'w' , 'o' , 'r' , 'l' , 'd' , '\n' , 0
str:
    .ascii "Ubuntu16.04.7-desktop-amd64\0"

.section .bss

.lcomm buf , 500

.section .text

.global _start
_start:
    pushl $buf
    pushl $-2536987
    call int2str
    addl $8 , %esp
    
    pushl %eax
    pushl $buf
    call byte_out
    addl $4 , %esp
    popl %ebx

    movl $SYS_EXIT , %eax
    int $0x80

# 功能 : 将 有符号整数 转换为 字符串
# 参数 1 : 32位 有符号整数
# 参数 2 : 存放 字符串 的内存地址
# 返回值 : %eax 中返回 字符串 长度，不包括终止符 '\0'
# 栈要求 : C 调用约定，存放参数
# 段要求 : 无
# 寄存器要求 :
#           %eax - 存放 整数
#           %ebx - 存放 字符串 的地址
# 注意 : 该函数内部调用 uint2str 函数
.type int2str , @function
int2str:
    movl 4(%esp) , %eax
    andl $0x80000000 , %eax
    cmpl $0 , %eax

    movl 4(%esp) , %eax
    movl 8(%esp) , %ebx

    jne int2str_neg
    jmp int2str_pos

int2str_neg:
    negl %eax
    movb $'-' , (%ebx)
    addl $1 , %ebx
int2str_pos:
    pushl %ebx
    pushl %eax
    call uint2str
    addl $4 , %esp
    popl %ebx
    cmpl %ebx , 8(%esp)
    jne int2str_addlen
    jmp int2str_exit
int2str_addlen:
    addl $1 , %eax
int2str_exit:
    ret

# 功能 : 将 无符号整数 转换为 字符串
# 参数 1 : 32位 无符号整数
# 参数 2 : 存放 字符串 的内存地址
# 返回值 : %eax 中返回 字符串 长度，不包括终止符 '\0'
# 栈要求 : C 调用约定，存放参数
# 段要求 : 无
# 寄存器要求 :
#           %ebx - 暂存 字符串 长度，除法需要
#           %ecx - 存放 字符串 的地址
#           %edx , %eax - 除法需要，互换需要
#           %esi , %edi - 索引
.type uint2str , @function
uint2str:
    movl 4(%esp) , %eax
    movl 8(%esp) , %ecx

    movl $0 , %edi
    movb $0 , (%ecx , %edi , 1)
    movl $10 , %ebx
uint2str_loop:
    movl $0 , %edx
    divl %ebx
    addl $TRAN_CHAR , %edx
    incl %edi
    movb %dl , (%ecx , %edi , 1)
    cmpl $0 , %eax
    jne uint2str_loop

    movl %edi , %ebx

    movl $0 , %esi
uint2str_invert:
    cmpl %esi , %edi
    jle uint2str_exit
    movb (%ecx , %esi , 1) , %al
    movb (%ecx , %edi , 1) , %dl
    movb %al , (%ecx , %edi , 1)
    movb %dl , (%ecx , %esi , 1)
    incl %esi
    decl %edi
    jmp uint2str_invert

uint2str_exit:
    movl %ebx , %eax
    ret

# 功能 : 输出字符串到文件描述符 out_fd 对应的文件
# 参数 1 : 字节数组地址，'\0' 结尾
# 返回值 : 同 write
# 栈要求 : C 调用约定，存放参数
# 段要求 : 需要使用 数据段 符号 out_fd
# 寄存器要求 : 同 write
.type byte_out , @function
byte_out:
    movl $SYS_WRITE , %eax
    movl out_fd , %ebx
    movl 4(%esp) , %ecx
    movl $0 , %edx
byte_out_loop:
    cmpb $0 , (%ecx , %edx , 1)
    je byte_out_loop_exit
    incl %edx
    jmp byte_out_loop
byte_out_loop_exit:
    int $0x80
    ret
