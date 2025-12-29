#include <linux/module.h>
#include <linux/platform_device.h>

// 平台设备的探测函数
static int plat_probe(struct platform_device* platdev)
{
    printk(KERN_INFO "probing platform device.\n");

    struct resource* res_mem;
    struct resource* res_irq;
    // 可直接访问 platform_device 结构体的资源数组，但这里
    // 使用 platform_get_resource() 获取硬件资源
    res_mem = platform_get_resource(platdev , IORESOURCE_MEM , 0);
    if(!res_mem)
    {
        dev_err(&platdev->dev, "failed to get memory resource.\n");
        return -ENODEV;
    }
    res_irq = platform_get_resource(platdev , IORESOURCE_IRQ , 0);
    if(!res_irq)
    {
        dev_err(&platdev->dev , "failed to get IRQ resource.\n");
        return -ENODEV;
    }

    printk("memory resource: start = 0x%llx, end = 0x%llx.\n" , res_mem->start , res_mem->end);
    printk("IRQ resource: number = %lld.\n" , res_irq->start);
    return 0;
}

// 平台设备的移除函数
static int plat_remove(struct platform_device* platdev)
{
    printk(KERN_INFO "removing platform device.\n");
    return 0;
}

// 定义平台驱动结构体
static struct platform_driver platdrv = {
    .probe = plat_probe,
    .remove = plat_remove,
    .driver = {
        .name = "custom_platdev_name", // 需与平台设备名一致，以便匹配。在 /sys/bus/platform/drivers 目录下
        .owner = THIS_MODULE,
    },
};

// 模块初始化函数
static int __init platdrv_init(void)
{
    int ret;
    ret = platform_driver_register(&platdrv); // 注册平台驱动
    if(ret)
    {
        printk(KERN_ERR "failed to register platform driver.\n");
        return ret;
    }
    printk(KERN_INFO "platform driver initialized.\n");
    return 0;
}

// 模块退出函数
static void __exit platdrv_exit(void)
{
    platform_driver_unregister(&platdrv); // 注销平台驱动
    printk(KERN_INFO "platform driver exited.\n");
}

module_init(platdrv_init);
module_exit(platdrv_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("BJ");