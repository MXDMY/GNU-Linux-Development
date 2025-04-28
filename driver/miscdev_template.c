#include <linux/init.h>       // 初始化头文件
#include <linux/module.h>     // 最基本的文件，支持动态添加和卸载模块。
#include <linux/miscdevice.h> // 注册杂项设备头文件
#include <linux/fs.h>         // 注册设备节点的文件结构体

struct file_operations miscdev_fops = {
    .owner = THIS_MODULE // 将 owner 字段指向本模块，避免模块中的相关操作正在被使用时卸载该模块
};

struct miscdevice miscdev = {
    .minor = MISC_DYNAMIC_MINOR, // 动态申请次设备号
    .name = "miscdev_name",      // 杂项设备名字
    .fops = &miscdev_fops,          // 文件操作集
};

static int __init miscdev_init(void)
{
    printk("miscdev_template init.\n");

    int ret;
    ret = misc_register(&miscdev); // 注册杂项设备
    if(ret < 0)
    {
        printk("misc_register is error.\n");
    }
    else
    {
        printk("misc_register is ok.\n");
    }
    printk("minor is %d.\n" , miscdev.minor);

    return 0;
}

static void __exit miscdev_exit(void)
{
    misc_deregister(&miscdev); // 注销杂项设备

    printk("miscdev_template exit.\n");
}

module_init(miscdev_init);
module_exit(miscdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("BJ");