#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>

#define MEM_START_ADDR 0xFEC20000
#define MEM_END_ADDR 0xFEC20004
#define IRQ_NUMBER 19

static struct resource platdev_resources[] = {
    {
        .start = MEM_START_ADDR, // 内存资源起始地址
        .end = MEM_END_ADDR,     // 内存资源结束地址
        .flags = IORESOURCE_MEM, // 标记为内存资源
    },
    {
        .start = IRQ_NUMBER,     // 中断资源号
        .end = IRQ_NUMBER,       // 中断资源号
        .flags = IORESOURCE_IRQ, // 标记为中断资源
    },
};

static void platdev_release(struct device* dev)
{
}

static struct platform_device platdev = {
    .name = "custom_platdev_name", // 设备名称，在 /sys/bus/platform/devices/ 目录下
    .id = -1,                      // 设备 ID
    .num_resources = ARRAY_SIZE(platdev_resources), // 资源数量
    .resource = platdev_resources,                  // 资源数组
    .dev.release = platdev_release,                 // 释放资源的回调函数
};

static int __init platdev_init(void)
{
    int ret;
    ret = platform_device_register(&platdev); // 注册平台设备
    if(ret)
    {
        printk(KERN_ERR "failed to register platform device.\n");
        return ret;
    }
    printk(KERN_INFO "platform device registered.\n");
    return 0;
}

static void __exit platdev_exit(void)
{
    platform_device_unregister(&platdev); // 注销平台设备
    printk(KERN_INFO "platform device unregistered.\n");
}

module_init(platdev_init);
module_exit(platdev_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("BJ");
