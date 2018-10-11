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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guillaume Lavigne");
MODULE_DESCRIPTION("DHT22 LKM");
MODULE_VERSION("1.0");

int err;
static unsigned gpio = 15; // (1-1) * 32 + 15
static unsigned int irq;
static uint16_t temp, hum, var;
static uint8_t chksum;


typedef struct{
	uint16_t hum;
	uint16_t temp;
	uint8_t chksum;
	
} __attribute__((packed,aligned(1))) values;

static values *val;

/**
 * Timing
*/
static int Tbe = 1000;

static struct kobject *dht22_kobject;

struct timeval now;

static uint32_t count = 0;
static int time1, time2;

static uint64_t raw_data=0;

DECLARE_COMPLETION(cpl);

/**
 * IRQ Handler 
*/
static irq_handler_t dht22_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
	count++;
	do_gettimeofday(&now);
	if (count < 2);
	// End of Frame
	if (count==42){
		time2=now.tv_usec;
		if (time2-time1 > 100)
			val->chksum = (val->chksum << 1) | 1;
		else
			val->chksum = (val->chksum << 1) | 0;
		complete(&cpl);
	}
	// Checksum
	if(count>=35){
		time2=now.tv_usec;
		if (time2-time1 > 100)
			val->chksum = (val->chksum << 1) | 1;
		else
			val->chksum = (val->chksum << 1) | 0;
		time1=time2;		
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

static void data_management(void){
	val = (values*) &raw_data;
	val->hum /=10;
	val->temp /=10;
	chksum = (raw_data & 0xff) + (raw_data & 0xff00) + (raw_data & 0xff0000) + (raw_data & 0xff000000);
	printk(KERN_INFO "CHK sent : %d, CHK computed : %d, raw_data : %d, temp = %d\n", val->chksum, chksum, raw_data, val->temp);
}

/**
 * DHT22 init frame
*/
static void dht22_initialize(void){
	count=0;
	gpio_direction_output(gpio,1);
	gpio_set_value(gpio,0);
	udelay(Tbe);
	enable_irq(irq);
	gpio_set_value(gpio,1);
	gpio_direction_input(gpio);

	wait_for_completion_interruptible(&cpl);
	disable_irq(irq);
	gpio_direction_output(gpio,0);
	data_management();
}

/**
 * Called on cat /sys/kernel/dht22_kobject/{temp,hum}
*/
static ssize_t b_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
	dht22_initialize();
	if (strcmp(attr->attr.name, "temp") == 0){
		var = temp;
	}

	else if(strcmp(attr->attr.name, "hum") == 0){
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
	irq = gpio_to_irq(gpio);
	
	err = request_irq(irq,
			(irq_handler_t) dht22_irq_handler,
			IRQF_TRIGGER_FALLING,
			"dht22_gpio_handler",
			NULL);
	disable_irq(irq);
	val =vmalloc(sizeof(values));
	return err;
}

/**
 * Exitting LKM
*/
static void __exit dht22_exit(void){
	gpio_unexport(gpio);
	gpio_free(gpio);
	kobject_put(dht22_kobject);
	free_irq(irq,NULL);
	vfree(val);
	printk(KERN_INFO "Removing DHT22 module\n");
}

module_init(dht22_init);
module_exit(dht22_exit);

