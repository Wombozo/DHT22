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
#include <linux/sched.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>

#define GPIO 15 // (1-1) * 32 + 15

typedef struct{
	unsigned int chksum : 8;
	int temp : 16;
	unsigned int hum : 16;
} __attribute__((packed,aligned(1))) values;

static unsigned int irq;
static uint16_t var;
static uint8_t chksum;

static values *val = NULL;
static int Tbe = 1000;
static struct kobject *dht22_kobject;
struct timeval now;
static uint32_t count = 0;
static int time1, time2;
static uint64_t raw_data=0;

DECLARE_COMPLETION(cpl);

/**
 * @brief IRQ Handler 
*/
static irq_handler_t dht22_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
	count++;
	do_gettimeofday(&now);
	if (count < 2);
	// End of Frame
	if (count>=42){
		time2=now.tv_usec;
		if (time2-time1 > 100)
			raw_data = (raw_data << 1) | 1;
		else
			raw_data = (raw_data << 1) | 0;
		complete(&cpl);
	}
	// Start of frame
	else if(count==2)
		time1=now.tv_usec;
	// Data
	else{
		time2=now.tv_usec;
		if (time2-time1 > 100)
			raw_data= raw_data << 1 | 1;
		else
			raw_data= raw_data << 1 | 0;
		time1=time2;
	}
	return (irq_handler_t) IRQ_HANDLED;
}

/**
 * @brief Data processing
*/
static int data_management(void){
	
	val = (values*) &raw_data;
	val->hum /=10;
	val->temp /=10;
	chksum = (raw_data & 0xff) + (raw_data & 0xff00) + (raw_data & 0xff0000) + (raw_data & 0xff000000);
	return (chksum != val->chksum);
}

/**
 * @brief DHT22 init frame
*/
static int dht22_initialize(void){
	int err;
	count=0;
	gpio_direction_output(GPIO,1);
	gpio_set_value(GPIO,0);
	udelay(Tbe);
	enable_irq(irq);
	gpio_set_value(GPIO,1);
	gpio_direction_input(GPIO);

	wait_for_completion(&cpl);
	disable_irq(irq);
	gpio_direction_output(GPIO,0);
	err = data_management();
	return err;
}

/**
 * @brief Called on cat /sys/kernel/dht22_kobject/{temp,hum}
*/
static ssize_t b_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
	while (dht22_initialize());
	
	if (strcmp(attr->attr.name, "temp") == 0){
		var = val->temp;
	}

	else if(strcmp(attr->attr.name, "hum") == 0){
		var = val->hum;
	}

	else{
		printk(KERN_WARNING "Invalid attr");
		gpio_set_value(GPIO,1);
		gpio_unexport(GPIO);
		gpio_free(GPIO);
		kobject_put(dht22_kobject);
		return -EINVAL;
	}
	reinit_completion(&cpl);
	return sprintf(buf, "%d\n", var);
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
 * @brief Initialize LKM
*/
static int __init dht22_init(void){
	int err;
	printk(KERN_DEBUG "Initializing DHT22 module\n");
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

	if (!gpio_is_valid(GPIO)){
		printk(KERN_ERR "Invalid GPIO number\n");
		return -ENODEV;
	}

	gpio_request(GPIO,"sysfs");
	gpio_export(GPIO,true);
	gpio_direction_output(GPIO,1);

	init_completion(&cpl);
	irq = gpio_to_irq(GPIO);
	
	err = request_irq(irq,
			(irq_handler_t) dht22_irq_handler,
			IRQF_TRIGGER_FALLING,
			"dht22_gpio_handler",
			NULL);
	disable_irq(irq);
	return err;
}

/**
 * @brief Exitting LKM
*/
static void __exit dht22_exit(void){
	gpio_unexport(GPIO);
	gpio_free(GPIO);
	kobject_put(dht22_kobject);
	free_irq(irq,NULL);
	printk(KERN_DEBUG "Removing DHT22 module\n");
}

module_init(dht22_init);
module_exit(dht22_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guillaume Lavigne");
MODULE_DESCRIPTION("DHT22 LKM");
MODULE_VERSION("1.0");
