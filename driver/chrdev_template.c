#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/uaccess.h> // copy_to_user copy_from_user
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/atomic.h>
#include <linux/timer.h>

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

#if 1
#define CHRDEV_TEMP_DEBUG(dev_num , str) printk(KERN_DEBUG "P%d:N%u:%s\n" , current->pid , dev_num , str)
#else
#define CHRDEV_TEMP_DEBUG(dev_num , str)
#endif

#define DEVICE_EXT_RW_WAITTIME 5 // 设备读写等待队列的超时时长，单位秒
#define DEVICE_EXT_KBUF_SIZE 32  // 设备缓冲区大小
#define DEVICE_EXT_COUNT 2       // 设备数量

struct device_ext
{
    dev_t dev_num;         // 设备号
    uint major;            // 主设备号
    uint minor;            // 次设备号
    struct cdev cdev;      // 要注册的字符设备
    struct class* class;   // 类
    struct device* device; // 设备
    char* class_name;      // 类名，在 /sys/class/ 目录下
    char* device_name;     // 设备节点名，在 /dev/ 和 /sys/class/<类名> 目录下

    char kbuf[DEVICE_EXT_KBUF_SIZE]; // 缓存区
    int alloc_step;        // 指示该设备的资源申请到了那一步，出错释放资源时使用，申请资源前务必先初始化为 0 值

/*
  该模板使用如下设备读写顺序机制：
  1、写独占；读允许其它线程读，但不允许写
  2、写完成，优先唤醒全部读，无读则唤醒一个写
  3、读完成，若仍有线程在读，则无操作，否则唤醒写
  读写顺序依赖的设备成员如下：
*/
    char is_free;                 // 空闲标志
    spinlock_t spinlock;          // 自旋锁
    wait_queue_head_t waithead;   // 写-等待队列头
    int reading_count;            // 在读线程数量
    wait_queue_head_t r_waithead; // 读-等待队列头

    struct fasync_struct* async_queue; // 异步通知队列,记录了注册异步通知的文件描述符和进程信息
};

static char* const chrdev_name = "chrdev_name"; // 设备名（只与主设备号关联，唯一），可在 /proc/devices 文件中查看
static const uint baseminor = 0; // 次设备号最小值

static struct device_ext dev[DEVICE_EXT_COUNT] = {
    {.class_name = "class1_name" , .device_name = "device1_name"},
    {.class_name = "class2_name" , .device_name = "device2_name"}
};

static atomic_t drvtime = ATOMIC_INIT(0); // 驱动被加载的时长，单位秒
static void drvtimer_func(struct timer_list* t); // drvtime 对应的定时功能函数
DEFINE_TIMER(drv_timer , drvtimer_func); // drvtime 对应的定时器
static void drvtimer_func(struct timer_list* t)
{
    atomic_inc(&drvtime);
    mod_timer(&drv_timer , jiffies + msecs_to_jiffies(1000)); // 重置
}

static int chrdev_open(struct inode* inode , struct file* file)
{
    file->private_data = container_of(inode->i_cdev , struct device_ext , cdev);

    return 0;
}

static ssize_t chrdev_read(struct file* file , char __user* buf , size_t size , loff_t* off)
{
    struct device_ext* dev = (struct device_ext*)file->private_data;
    unsigned long irqflags = 0;
    ssize_t ret = -1;

CHRDEV_READ_START:
    CHRDEV_TEMP_DEBUG(dev->dev_num , "r trylock");
    spin_lock_irqsave(&dev->spinlock , irqflags);
    if(! dev->is_free)
    {
        if(dev->reading_count <= 0) // 设备忙碌且无读线程，写在独占
        {
            spin_unlock_irqrestore(&dev->spinlock , irqflags);
            if(file->f_flags & O_NONBLOCK)
            {
                ret = -EAGAIN; // EWOULDBLOCK
                CHRDEV_TEMP_DEBUG(dev->dev_num , "r failed:block");
                goto CHRDEV_READ_END;
            }
            CHRDEV_TEMP_DEBUG(dev->dev_num , "r wait");
            // 睡眠中断返回 ERESTARTSYS，暂未处理
            wait_event_interruptible_timeout(dev->r_waithead , dev->is_free , DEVICE_EXT_RW_WAITTIME * HZ);
            goto CHRDEV_READ_START;
        }
        else
        {
            dev->reading_count++;
            spin_unlock_irqrestore(&dev->spinlock , irqflags);
        }
    }
    else
    {
        dev->is_free = 0;
        dev->reading_count++;
        spin_unlock_irqrestore(&dev->spinlock , irqflags);
    }
    CHRDEV_TEMP_DEBUG(dev->dev_num , "r...");

    /* Read */
    if(DEVICE_EXT_KBUF_SIZE == *off)
    {
        ret = 0;
    }
    else
    {
        if(size > DEVICE_EXT_KBUF_SIZE - *off)
            ret = DEVICE_EXT_KBUF_SIZE - *off;
        else
            ret = size;
        ret -= copy_to_user(buf , dev->kbuf + *off , ret); // 拷贝可能失败，获取成功拷贝的字节数
        *off += ret;
    }
    /********/

    CHRDEV_TEMP_DEBUG(dev->dev_num , "r end");
    spin_lock_irqsave(&dev->spinlock , irqflags);
    dev->reading_count--;
    if(dev->reading_count <= 0)
    {
        dev->is_free = 1;
        spin_unlock_irqrestore(&dev->spinlock , irqflags);
        if(waitqueue_active(&dev->waithead))
        {
            CHRDEV_TEMP_DEBUG(dev->dev_num , "wake w");
            wake_up_interruptible(&dev->waithead);
        }
    }
    else
    {
        spin_unlock_irqrestore(&dev->spinlock , irqflags);
    }

CHRDEV_READ_END:
    return ret;
}

static ssize_t chrdev_write(struct file* file , const char __user* buf , size_t size , loff_t* off)
{
    struct device_ext* dev = (struct device_ext*)file->private_data;
    unsigned long irqflags = 0;
    ssize_t ret = -1;

CHRDEV_WRITE_START:
    CHRDEV_TEMP_DEBUG(dev->dev_num , "w trylock");
    spin_lock_irqsave(&dev->spinlock , irqflags);
    if(! dev->is_free)
    {
        spin_unlock_irqrestore(&dev->spinlock , irqflags);
        if(file->f_flags & O_NONBLOCK)
        {
            ret = -EAGAIN; // EWOULDBLOCK
            CHRDEV_TEMP_DEBUG(dev->dev_num , "w failed:block");
            goto CHRDEV_WRITE_END;
        }
        CHRDEV_TEMP_DEBUG(dev->dev_num , "w wait");
        // 睡眠中断返回 ERESTARTSYS，暂未处理
        wait_event_interruptible_timeout(dev->waithead , dev->is_free , DEVICE_EXT_RW_WAITTIME * HZ);
        goto CHRDEV_WRITE_START;
    }
    else
    {
        dev->is_free = 0;
        spin_unlock_irqrestore(&dev->spinlock , irqflags);
        CHRDEV_TEMP_DEBUG(dev->dev_num , "w...");
    }
    
    /* Write */
    if(DEVICE_EXT_KBUF_SIZE == *off)
    {
        ret = 0;
    }
    else
    {
        if(size > DEVICE_EXT_KBUF_SIZE - *off)
            ret = DEVICE_EXT_KBUF_SIZE - *off;
        else
            ret = size;
        ret -= copy_from_user(dev->kbuf + *off , buf , ret); // 拷贝可能失败，获取成功拷贝的字节数
        *off += ret;
    }
    /********/

    CHRDEV_TEMP_DEBUG(dev->dev_num , "w end");
    dev->is_free = 1;
    if(waitqueue_active(&dev->r_waithead))
    {
        CHRDEV_TEMP_DEBUG(dev->dev_num , "wake all r");
        wake_up_interruptible_all(&dev->r_waithead);
    }
    else if(waitqueue_active(&dev->waithead))
    {
        CHRDEV_TEMP_DEBUG(dev->dev_num , "wake other w");
        wake_up_interruptible(&dev->waithead);
    }
    else // 优先处理等待队列的读写，然后再处理信号读
    {
        // CHRDEV_TEMP_DEBUG(dev->dev_num , "sigio r");
        // kill_fasync(&dev->async_queue , SIGIO , POLLIN);
    }

CHRDEV_WRITE_END:
    return ret;
}

static __poll_t chrdev_poll(struct file* file , struct poll_table_struct* p)
{
    struct device_ext* dev = (struct device_ext*)file->private_data;
    __poll_t mask = 0;

    poll_wait(file , &dev->waithead , p);
    poll_wait(file , &dev->r_waithead , p);

    if(dev->is_free)
        mask |= (POLLIN | POLLOUT);
    else if(dev->reading_count > 0)
        mask |= POLLIN;

    return mask;
}

static int chrdev_fasync(int fd , struct file* file , int on)
{
    struct device_ext* dev = (struct device_ext*)file->private_data;
    return fasync_helper(fd , file , on , &dev->async_queue);
}

static loff_t chrdev_llseek(struct file* file , loff_t offset , int whence)
{
    loff_t new_offset = file->f_pos;

    switch(whence)
    {
    case SEEK_SET:
        if(offset < 0 || offset > DEVICE_EXT_KBUF_SIZE)
            return -EINVAL;
        new_offset = offset;
        break;

    case SEEK_CUR:
        if(file->f_pos + offset > DEVICE_EXT_KBUF_SIZE || file->f_pos + offset < 0)
            return -EINVAL;
        new_offset = file->f_pos + offset;
        break;

    case SEEK_END:
        if(offset > 0 || offset < -DEVICE_EXT_KBUF_SIZE)
            return -EINVAL;
        new_offset = DEVICE_EXT_KBUF_SIZE + offset;
        break;

    default:
        break;
    }

    file->f_pos = new_offset;
    return new_offset;
}

static int chrdev_release(struct inode* inode , struct file* file)
{
    chrdev_fasync(-1 , file , 0);

    return 0;
}

static struct file_operations cdev_fops = {
    .owner = THIS_MODULE, // 将 owner 字段指向本模块，避免模块中的相关操作正在被使用时卸载该模块
    .open = chrdev_open,
    .read = chrdev_read,
    .write = chrdev_write,
    .poll = chrdev_poll,
    .fasync = chrdev_fasync,
    .llseek = chrdev_llseek,
    .release = chrdev_release,
};

static int __init chrdev_init(void) // 驱动入口函数
{
    printk(KERN_INFO "chrdev_template init...\n");

    dev_t dev_num;
    int ret = alloc_chrdev_region(&dev_num , baseminor , DEVICE_EXT_COUNT , chrdev_name); // 自动申请设备号
    if(ret < 0)
    {
        goto CHRDEV_INIT_ALLOC_DEV_NUM_FAILED;
    }
    uint major , minor;
    major = MAJOR(dev_num); // 获取申请的主设备号
    minor = MINOR(dev_num); // 获取申请的次设备号起点
    printk(KERN_INFO "major is %u.\nminor start is %u.\n" , major , minor);

    int i = 0;
    for(i = 0 ; i < DEVICE_EXT_COUNT ; i++)
    {
        dev[i].alloc_step = 0;

        dev[i].is_free = 1;
        spin_lock_init(&dev[i].spinlock);
        init_waitqueue_head(&dev[i].waithead);
        dev[i].reading_count = 0;
        init_waitqueue_head(&dev[i].r_waithead);

        dev[i].major = major;
        dev[i].minor = minor + i;
        dev[i].dev_num = MKDEV(major , dev[i].minor);
        cdev_init(&dev[i].cdev , &cdev_fops); // 初始化 cdev 结构体，并链接到 cdev_fops 结构体
        dev[i].cdev.owner = THIS_MODULE; // 将 owner 字段指向本模块，避免模块中的相关操作正在被使用时卸载该模块
        
        // 进行设备号与 cdev 的关联，每次关联一个。在 struct device_ext 中，cdev 与 dev_num 是一对一的，这样做，
        // 有利于 chrdev_open 部分通过 container_of 与 cdev 快速定位具体设备，而不需要通过 inode->i_rdev 挨个找
        ret = cdev_add(&dev[i].cdev , dev[i].dev_num , 1);
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

    printk(KERN_INFO "chrdev_template init ok.\n");
    add_timer(&drv_timer);
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
CHRDEV_INIT_ALLOC_DEV_NUM_FAILED:
    printk(KERN_CRIT "chrdev_template init failed.\n");
    return ret;
}

static void __exit chrdev_exit(void) // 驱动出口函数
{
    del_timer(&drv_timer);
    int i = 0;
    for(i = 0 ; i < DEVICE_EXT_COUNT ; i++)
    {
        device_destroy(dev[i].class , dev[i].dev_num); // 删除创建的设备
        class_destroy(dev[i].class); // 删除创建的类
        cdev_del(&dev[i].cdev); // 删除添加的字符设备
        unregister_chrdev_region(dev[i].dev_num , 1); // 释放字符设备所申请的设备号
    }

    printk(KERN_INFO "chrdev_template exit.\n");
}

module_init(chrdev_init); // 注册入口函数
module_exit(chrdev_exit); // 注册出口函数
MODULE_LICENSE("GPL v2"); // 同意 GPL 开源协议
MODULE_AUTHOR("BJ");      // 作者信息
