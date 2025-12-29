SYSSIZE = 0x3000

SETUPLEN = 4

# x86 架构标准行为：
# BIOS 启动后会将引导扇区（Boot Sector）加载到物理地址 0x7C00 处
BOOTSEG = 0x7c0

INITSEG = 0x9000

SETUPSEG = 0x9020
SYSSEG = 0x1000
ENDSEG = SYSSEG + SYSSIZE
ROOT_DEV  = 0x000

# 生成 16 位代码，实模式内存空间 2^(16 + 4)
# 16 位段地址 + 4 位偏移
.code16
.text

.global _start
_start:
    jmpl $BOOTSEG , $start2 # 实模式下跳转到：物理地址 = 段选择子（BOOTSEG） * 16 + 偏移量（start2）

start2:
    # x86 指令集规定，不能将立即数直接移到段寄存器 cs ds ss es fs gs
    movw $BOOTSEG , %ax
    movw %ax, %ds       # 数据段（Data Segment）寄存器设置
    movw $INITSEG, %ax
    movw %ax, %es       # 附加段（Extra Segment）寄存器设置
    movw $256, %cx      # 计数器寄存器设置
    subw %si, %si       # 源变址寄存器清 0
    subw %di, %di       # 目的变址寄存器清 0
    rep                 # do { <next instruction>, cx--, } while(0 == cx)
    # ​​串操作指令​​，从 ds:si ​​复制一个 word（16 位）到 es:di 指向的地址，
    # 若 DF 为 0（通常由 cld 指令提前清除），则 si 和 di += 2，
    # 若 DF 为 1（由 std 指令设置），则 si 和 di -= 2
    movsw
    # 上述操作将 BOOTSEG 开始的 1 扇区（256 * 2 byte）代码搬移到了 INITSEG

    jmpl $INITSEG, $go  # 远跳转（段间跳转到另一个代码段），会同时改变 cs 至 INITSEG

go:
    # 初始化段寄存器与 sp 寄存器至合理位置
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0xFF00, %sp

load_setup:
    movw $0x0000, %dx   # dh 磁头号；dl 驱动器号：0 软盘、7 硬盘
    movw $0x0002, %cx   # ch 柱面号低 8 位；cl 0-5 位扇区号，6-7 位代表磁道号高 2 位
    movw $0x0200, %bx   # es:bx 指向数据缓冲区
    movb $SETUPLEN, %al # 扇区数量
    movb $0x02, %ah     # 功能号：读磁盘扇区到内存
    int $0x13           # 读磁盘数据进内存 0x90200
    jnc ok_load_setup   # CF 被 0x13 中断 用来存放磁盘操作结果，成功则不置位
    movw $0x0000, %dx
    movw $0x0000, %ax   # ah = 0，重置磁盘
    int $0x13
    jmp load_setup

ok_load_setup:
    movw $msg, %ax
    movw %ax, %bp
    movw $0x01301, %ax
    movw $0x0c, %bx
    movw $21, %cx
    movb $0, %dl
    int $0x010          # 显示器相关中断

    jmpl $SETUPSEG, $0

msg:
    .ascii "Setup has been loaded"

.org 508 # 设置当前代码或数据的起始地址
root_dev:
    .word ROOT_DEV
boot_flag:
    .word 0xaa55
