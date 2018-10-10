/**
 * @file dht22.c
 * @author Guillaume Lavigne
 * @date 08 October 2018
 * @brief A kernel loadable module for controlling the DHT22 by GPIO.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/timekeeping.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guillaume Lavigne");
MODULE_DESCRIPTION("DHT22 LKM");
MODULE_VERSION("1.0");

int err;
static unsigned gpio = 15; // (1-1) * 32 + 15
//static int temp, hum;

/**
 * Timing
*/
static int Tbe = 1000;
//static int Tgo = 30;
//static int Trel = 80;
//static int Treh = 80;
//static int Tlow = 50;
//static int Tho = 26;
//static int Thi = 70;
//static int Ten = 50;

static struct kobject *dht22_kobject;

struct timeval now;

struct tasklet_struct dht22_tasklet;

static int count = 0;
static int time1, time2;

static char bit_buf[32];
static int index=0;

DECLARE_COMPLETION(cpl);

static void dht22_bhalf(unsigned long ctx);

/**
 * Bottom half function (multi-threaded)
*/
static void dht22_bhalf(unsigned long time){
	if (count < 5);
	else if(count>=10)
		complete(&cpl);
	else if(count%2==1){
		time1=time;
	}
	else{
		time2=time;
		if (time2-time1 > 50)
			bit_buf[index]=1;
		else
			bit_buf[index]=0;
		index++;
	}
	
}

/**
 * IRQ Handler 
*/
static irq_handler_t dht22_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
	count++;
	do_gettimeofday(&now);
	tasklet_init(&dht22_tasklet, dht22_bhalf,(unsigned long)(now.tv_usec));
	tasklet_schedule(&dht22_tasklet);
	return (irq_handler_t) IRQ_HANDLED;
}

/**
 * Called on cat /sys/kernel/dht22_kobject/{temp,hum}
*/
static ssize_t b_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
	gpio_direction_output(gpio,1);
	//int var;
	count=0;
	gpio_set_value(gpio,0);
	udelay(Tbe);
	gpio_set_value(gpio,1);
	gpio_direction_input(gpio);

	wait_for_completion(&cpl);
	printk(KERN_INFO "Buffer :%s , index :%d , count :%d\n",bit_buf,index,count);

/*
	if (strcmp(attr->attr.name, "temp") == 0){
		printk(KERN_INFO "Reading temperature\n");
		var = temp;
	}

	else if(strcmp(attr->attr.name, "hum") == 0){
		printk(KERN_INFO "Reading humidity\n");
		var = hum;
	}

	else{
		printk(KERN_WARNING "Invalid attr");
		gpio_set_value(gpio,1);
		gpio_unexport(gpio);
		gpio_free(gpio);
		kobject_put(dht22_kobject);
		return -EINVAL;
	}
*/
//	return sprintf(buf, "%d\n", var);
	return 0;
}

/**
 * Attributes (temperature, humidity)
*/
static struct kobj_attribute temp_attr =
	__ATTR(temp, 0644, b_show, NULL);

static struct kobj_attribute hum_attr =
	__ATTR(hum, 0644, b_show, NULL);

static struct attribute *attrs[] = {
	&temp_attr.attr,
	&hum_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};



/**
 * Initialize LKM
*/
static int __init dht22_init(void){
	printk(KERN_INFO "Initializing DHT22 module\n");
	dht22_kobject = kobject_create_and_add("dht22_kobj",kernel_kobj);
	if (!dht22_kobject){
		printk(KERN_ERR "Cannot create kobject\n");
		return -ENOMEM;	
	}

	err = sysfs_create_group(dht22_kobject, &attr_group);
	if (err){
		printk(KERN_ERR "Cannot create group for kobj %s\n", dht22_kobject->name);
		kobject_put(dht22_kobject);
		return -ENOMEM;
	}

	if (!gpio_is_valid(gpio)){
		printk(KERN_ERR "Invalid GPIO number\n");
		return -ENODEV;
	}

	gpio_request(gpio,"sysfs");
	gpio_export(gpio,true);
	gpio_direction_output(gpio,1);

	init_completion(&cpl);

	err = request_irq(gpio_to_irq(gpio),
			(irq_handler_t) dht22_irq_handler,
			IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,
			"dht22_gpio_handler",
			NULL);
	
	return err;
}

/**
 * Exitting LKM
*/
static void __exit dht22_exit(void){
	gpio_set_value(gpio,1);
	gpio_unexport(gpio);
	gpio_free(gpio);
	kobject_put(dht22_kobject);
	tasklet_kill(&dht22_tasklet);
	free_irq(gpio_to_irq(gpio),NULL);
	printk(KERN_INFO "Removing DHT22 module\n");
}

module_init(dht22_init);
module_exit(dht22_exit);

