#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/uaccess.h> // copy_to_user copy_from_user

// 驱动模块传参，insmod 命令操作时赋值可覆盖代码中的值，可在 /sys/module/<驱动文件名>/parameters/ 下查看
/*static int number = 123;
static char* name = "hello,dristtudy";
static int para[8] = {0 , -1 , 2};
static char str1[10] = "hello";
static int n_para = 2;    //定义 int 类型的用来记录 module_param_array 函数传递数组元素个数的变量 n_para
module_param(number , int , 00660); //传递 int 类型的参数 number
module_param(name , charp , S_IRUGO); //传递 char 类型变量 name，S_IRUGO 表示权限为可读
module_param_array(para , int , &n_para , S_IRUGO);    //传递 int 类型的数组变量 para
module_param_string(str , str1 , sizeof(str1) , S_IRUGO); //传递字符串类型的变量 str1
*/
// 导出到外部符号，可被别的驱动程序引用
/*int num = 10;
EXPORT_SYMBOL(num);  
int add(int a, int b)
{
    return a + b;
}
EXPORT_SYMBOL(add);*/

struct device_ext
{
    dev_t dev_num;         // 设备号
    uint major;            // 主设备号
    uint minor;            // 次设备号
    struct cdev cdev;      // 要注册的字符设备
    struct class* class;   // 类
    struct device* device; // 设备

#define DEVICE_EXT_KBUF_SIZE 32
    char kbuf[DEVICE_EXT_KBUF_SIZE]; // 缓存区

    char* class_name;      // 类名，在 /sys/class/ 目录下
    char* device_name;     // 设备节点名，在 /dev/ 和 /sys/class/<类名> 目录下

    int alloc_step;        // 指示该设备的资源申请到了那一步，出错释放资源时使用，申请资源前务必先初始化为 0 值
};

static char* const chrdev_name = "chrdev_name"; // 设备名（只与主设备号关联，唯一），可在 /proc/devices 文件中查看
static const uint baseminor = 0; // 次设备号最小值

#define DEVICE_EXT_COUNT 2
static struct device_ext dev[DEVICE_EXT_COUNT] = {
    {.class_name = "class1_name" , .device_name = "device1_name" , .alloc_step = 0},
    {.class_name = "class2_name" , .device_name = "device2_name" , .alloc_step = 0}
};

static int chrdev_open(struct inode* inode , struct file* file)
{
    printk("chrdev_open is called.\n");

    file->private_data = container_of(inode->i_cdev , struct device_ext , cdev);

    return 0;
}

static ssize_t chrdev_read(struct file* file , char __user* buf , size_t size , loff_t* off)
{
    printk("chrdev_read is called.\n");
    return size;
}

static ssize_t chrdev_write(struct file* file , const char __user* buf , size_t size , loff_t* off)
{
    printk("chrdev_write is called.\n");
    return size;
}

static int chrdev_release(struct inode* inode , struct file* file)
{
    printk("chrdev_release is called.\n");
    return 0;
}

static struct file_operations cdev_fops = {
    .owner = THIS_MODULE, // 将 owner 字段指向本模块，避免模块中的相关操作正在被使用时卸载该模块
    .open = chrdev_open,
    .read = chrdev_read,
    .write = chrdev_write,
    .release = chrdev_release,
};

static int __init chrdev_init(void) // 驱动入口函数
{
    printk("chrdev_template init.\n");

    dev_t dev_num;
    int ret = alloc_chrdev_region(&dev_num , baseminor , DEVICE_EXT_COUNT , chrdev_name); // 自动申请设备号
    if(ret < 0)
    {
        return ret;
    }
    uint major , minor;
    major = MAJOR(dev_num); // 获取申请的主设备号
    minor = MINOR(dev_num); // 获取申请的次设备号起点
    printk("major is %u.\nminor start is %u.\n" , major , minor);

    int i = 0;
    for(i = 0 ; i < DEVICE_EXT_COUNT ; i++)
    {
        dev[i].major = major;
        dev[i].minor = minor + i;
        dev[i].dev_num = MKDEV(major , dev[i].minor);
        cdev_init(&dev[i].cdev , &cdev_fops); // 初始化 cdev 结构体，并链接到 cdev_fops 结构体
        dev[i].cdev.owner = THIS_MODULE; // 将 owner 字段指向本模块，避免模块中的相关操作正在被使用时卸载该模块

        ret = cdev_add(&dev[i].cdev , dev[i].dev_num , 1); // 进行设备号与 cdev 的关联
        if(ret < 0)
        {
            goto CHRDEV_INIT_FAILED;
        }
        dev[i].alloc_step++;

        dev[i].class = class_create(THIS_MODULE , dev[i].class_name); // 进行类的创建，类名称为 class_name
        if(IS_ERR(dev[i].class))
        {
            ret = PTR_ERR(dev[i].class);
            goto CHRDEV_INIT_FAILED;
        }
        dev[i].alloc_step++;

        dev[i].device = device_create(dev[i].class , NULL , 
                        dev[i].dev_num , NULL , dev[i].device_name); // 进行设备的创建，设备名称为 device_name
        if(IS_ERR(dev[i].device))
        {
            ret = PTR_ERR(dev[i].device);
            goto CHRDEV_INIT_FAILED;
        }
        dev[i].alloc_step++;
    }

    return 0;

CHRDEV_INIT_FAILED:
    for(i = 0 ; i < DEVICE_EXT_COUNT ; i++)
    {
        switch (dev[i].alloc_step)
        {
        case 3:
            device_destroy(dev[i].class , dev[i].dev_num);
        case 2:
            class_destroy(dev[i].class);
        case 1:
            cdev_del(&dev[i].cdev);
        default:
            unregister_chrdev_region(dev[i].dev_num , 1);
            break;
        }
    }
    return ret;
}

static void __exit chrdev_exit(void) // 驱动出口函数
{
    int i = 0;
    for(i = 0 ; i < DEVICE_EXT_COUNT ; i++)
    {
        device_destroy(dev[i].class , dev[i].dev_num); // 删除创建的设备
        class_destroy(dev[i].class); // 删除创建的类
        cdev_del(&dev[i].cdev); // 删除添加的字符设备
        unregister_chrdev_region(dev[i].dev_num , 1); // 释放字符设备所申请的设备号
    }

    printk("chrdev_template exit.\n");
}

module_init(chrdev_init); // 注册入口函数
module_exit(chrdev_exit); // 注册出口函数
MODULE_LICENSE("GPL v2"); // 同意 GPL 开源协议
MODULE_AUTHOR("BJ");      // 作者信息
