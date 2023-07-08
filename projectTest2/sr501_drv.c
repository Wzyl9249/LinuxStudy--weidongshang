#include <linux/module.h>
#include <linux/poll.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/current.h>
#include <linux/kthread.h>



/* 主设备号                                                                 */
static int major;
static struct class *sr501_class;
static struct gpio_desc *sr501_gpio;
static int irq;
static int sr501_data = 0;  
static wait_queue_head_t sr501_wq;
static struct task_struct *sr501_kthread;


/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t sr501_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int val;
	int len = (size < 4) ? size : 4;

	wait_event_interruptible(sr501_wq, sr501_data);
	val = copy_to_user(buf,&sr501_data,len);
	sr501_data = 0;
	return len;


}



/* 定义自己的file_operations结构体                                              */
static struct file_operations sr501_fops = {
	.owner	 = THIS_MODULE,	
	.read    = sr501_drv_read,
};


static irqreturn_t sr501_isr(int irq, void *dev_id)
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	sr501_data = 1;
	wake_up(&sr501_wq);
	return IRQ_WAKE_THREAD;
}

/* 1. 从platform_device获得GPIO
 * 2. gpio=>irq
 * 3. request_irq
 */
static int sr501_probe(struct platform_device *pdev)
{
	sr501_gpio = gpiod_get(&pdev->dev,NULL,0);
	gpiod_direction_input(sr501_gpio);

	irq = gpiod_to_irq(sr501_gpio);
	request_irq(irq,sr501_isr,IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,"sr501",NULL);
	
	device_create(sr501_class, NULL, MKDEV(major, 0), NULL, "100ask_sr501"); /* /dev/100ask_led0 */
    return 0;
}

static int sr501_remove(struct platform_device *pdev)
{
	device_destroy(sr501_class, MKDEV(major, 0));
	free_irq(irq,NULL);
	gpiod_put(sr501_gpio);
    
    return 0;
}



static const struct of_device_id ask100_sr501[] = {
    { .compatible = "100ask,sr501" },
    { },
};


/* 1. 定义platform_driver */
static struct platform_driver sr501_driver = {
    .probe      = sr501_probe,
    .remove     = sr501_remove,
    .driver     = {
        .name   = "sr501",
        .of_match_table = ask100_sr501,
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init sr501_init(void)
{
    int err;
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	sr501_kthread = kthread_run(sr501_thread_func, NULL, "sr501d");
	return 0;

	major = register_chrdev(0, "100ask_sr501", &sr501_fops);  /* /dev/led */

	sr501_class = class_create(THIS_MODULE, "100ask_sr501_class");
	if (IS_ERR(sr501_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "sr501");
		gpiod_put(sr501_gpio);
		return PTR_ERR(sr501_class);
	}

    err = platform_driver_register(&sr501_driver); 
	
	return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 *     卸载platform_driver
 */
static void __exit sr501_exit(void)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	kthread_stop(sr501_kthread);
	return ;
	
    platform_driver_unregister(&sr501_driver);
	class_destroy(sr501_class);
	unregister_chrdev(major, "100ask_sr501");
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(sr501_init);
module_exit(sr501_exit);

MODULE_LICENSE("GPL");


