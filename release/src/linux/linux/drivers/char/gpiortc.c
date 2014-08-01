/*
 * Real Time Clock driver for WL-HDD
 *
 * Copyright (C) 2007 Andreas Engel
 *
 * Hacked together mostly by copying the relevant code parts from:
 *   drivers/i2c/i2c-bcm5365.c
 *   drivers/i2c/i2c-algo-bit.c
 *   drivers/char/rtc.c
 *
 * Note 1:
 * This module uses the standard char device (10,135), while the Asus module
 * rtcdrv.o uses (12,0). So, both can coexist which might be handy during
 * development (but see the comment in rtc_open()).
 *
 * Note 2:
 * You might need to set the clock once after loading the driver the first
 * time because the driver switches the chip into 24h mode if it is running
 * in 12h mode.
 *
 * Usage:
 * For compatibility reasons with the original asus driver, the time can be
 * read and set via the /dev/rtc device entry. The only accepted data format
 * is "YYYY:MM:DD:W:HH:MM:SS\n". See OpenWrt wiki for a script which handles
 * this format.
 *
 * In addition, this driver supports the standard ioctl() calls for setting
 * and reading the hardware clock, so the ordinary hwclock utility can also
 * be used.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * TODO:
 * - add a /proc/driver/rtc interface?
 * - make the battery failure bit available through the /proc interface?
 *
 * $Id: rtc.c 7 2007-05-25 19:37:01Z ae $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <typedefs.h>
#include <bcmutils.h>
#include <sbutils.h>
#include <bcmdevs.h>

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/

/* Can be changed via a module parameter */
static int rtc_debug = 0;
static int sda_mask = 0;
static int scl_mask = 0;

static unsigned long rtc_status = 0;	/* bitmapped status byte.	*/

static spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

static sb_t *gpio_sbh;

#define I2C_READ_MASK  1
#define I2C_WRITE_MASK 0

#define I2C_ACK 1
#define I2C_NAK 0

#define RTC_EPOCH		1900
#define RTC_I2C_ADDRESS		(0x32 << 1)
#define RTC_24HOUR_MODE_MASK	0x20
#define RTC_PM_MASK		0x20
#define RTC_VDET_MASK		0x40
#define RTC_Y2K_MASK		0x80

/*
 * Delay in microseconds for generating the pulses on the I2C bus. We use
 * a rather conservative setting here. See datasheet of the RTC chip.
 */
#define ADAP_DELAY 50

/* Avoid spurious compiler warnings */
#define UNUSED __attribute__((unused))

static void sdalo(void)
{
	sb_gpioouten(gpio_sbh, sda_mask, sda_mask, GPIO_HI_PRIORITY);
	udelay(ADAP_DELAY);
}

static void sdahi(void)
{
	sb_gpioouten(gpio_sbh, sda_mask, 0, GPIO_HI_PRIORITY);
	udelay(ADAP_DELAY);
}

static void scllo(void)
{
	sb_gpioouten(gpio_sbh, scl_mask, scl_mask, GPIO_HI_PRIORITY);
	udelay(ADAP_DELAY);
}

static int getscl(void)
{
	return (sb_gpioin(gpio_sbh) & scl_mask) != 0;
}

static int getsda(void)
{
	return (sb_gpioin(gpio_sbh) & sda_mask) != 0;
}

/*
 * We shouldn't simply set the SCL pin to high. Like SDA, the SCL line is
 * bidirectional too. According to the I2C spec, the slave is allowed to
 * pull down the SCL line to slow down the clock, so we need to check this.
 * Generally, we'd need a timeout here, but in our case, we just check the
 * line, assuming the RTC chip behaves well.
 */
static int sclhi(void)
{
	sb_gpioouten(gpio_sbh, scl_mask, 0, GPIO_HI_PRIORITY);
	udelay(ADAP_DELAY);
	if (!getscl()) {
		printk(KERN_ERR "SCL pin should be low\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static void i2c_start(void)
{
	sdalo();
	scllo();
}

static void i2c_stop(void)
{
	sdalo();
	sclhi();
	sdahi();
}

static int i2c_outb(int c)
{
	int i;
	int ack;

	/* assert: scl is low */
	for (i = 7; i >= 0; i--) {
		if (c & ( 1 << i )) {
			sdahi();
		} else {
			sdalo();
		}
		if (sclhi() < 0) { /* timed out */
			sdahi(); /* we don't want to block the net */
			return -ETIMEDOUT;
		};
		scllo();
	}
	sdahi();
	if (sclhi() < 0) {
		return -ETIMEDOUT;
	};
	/* read ack: SDA should be pulled down by slave */
	ack = getsda() == 0;	/* ack: sda is pulled low ->success.	 */
	scllo();

	if (rtc_debug)
		printk(KERN_DEBUG "i2c_outb(0x%02x) -> %s\n",
		       c, ack ? "ACK": "NAK");

	return ack;		/* return 1 if device acked	 */
	/* assert: scl is low (sda undef) */
}

static int i2c_inb(int ack)
{
	int i;
	unsigned int indata = 0;

	/* assert: scl is low */

	sdahi();
	for (i = 0; i < 8; i++) {
		if (sclhi() < 0) {
			return -ETIMEDOUT;
		};
		indata *= 2;
		if (getsda())
			indata |= 0x01;
		scllo();
	}
	if (ack) {
		sdalo();
	} else {
		sdahi();
	}

	if (sclhi() < 0) {
		sdahi();
		return -ETIMEDOUT;
	}
	scllo();
	sdahi();

	if (rtc_debug)
		printk(KERN_DEBUG "i2c_inb() -> 0x%02x\n", indata);

	/* assert: scl is low */
	return indata & 0xff;
}

static void i2c_init(void)
{
	sb_gpiocontrol(gpio_sbh, sda_mask | scl_mask, 0, GPIO_HI_PRIORITY);
	sb_gpioout(gpio_sbh, sda_mask | scl_mask, 0, GPIO_HI_PRIORITY);
	sdahi();
	sclhi();
}

static int rtc_open(UNUSED struct inode *inode, UNUSED struct file *filp)
{
	spin_lock_irq(&rtc_lock);

	if (rtc_status & RTC_IS_OPEN) {
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}

	rtc_status |= RTC_IS_OPEN;

	/*
	 * The following call is only necessary if we use both this driver and
	 * the proprietary one from asus at the same time (which, b.t.w. only
	 * makes sense during development). Otherwise, each access via the asus
	 * driver will make access via this driver impossible.
	 */
	i2c_init();

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int rtc_release(UNUSED struct inode *inode, UNUSED struct file *filp)
{
	/* No need for locking here. */
	rtc_status &= ~RTC_IS_OPEN;
	return 0;
}

static int from_bcd(int bcdnum)
{
	int fac, num = 0;

	for (fac = 1; bcdnum; fac *= 10) {
		num += (bcdnum % 16) * fac;
		bcdnum /= 16;
	}

	return num;
}

static int to_bcd(int decnum)
{
	int fac, num = 0;

	for (fac = 1; decnum; fac *= 16) {
		num += (decnum % 10) * fac;
		decnum /= 10;
	}

	return num;
}

static void get_rtc_time(struct rtc_time *rtc_tm)
{
	int cr2;

	/*
	 * Read date and time from the RTC. We use read method (3).
	 */

	i2c_start();
	i2c_outb(RTC_I2C_ADDRESS | I2C_READ_MASK);
	cr2             = i2c_inb(I2C_ACK);
	rtc_tm->tm_sec  = i2c_inb(I2C_ACK);
	rtc_tm->tm_min  = i2c_inb(I2C_ACK);
	rtc_tm->tm_hour = i2c_inb(I2C_ACK);
	rtc_tm->tm_wday = i2c_inb(I2C_ACK);
	rtc_tm->tm_mday = i2c_inb(I2C_ACK);
	rtc_tm->tm_mon  = i2c_inb(I2C_ACK);
	rtc_tm->tm_year = i2c_inb(I2C_NAK);
	i2c_stop();

	if (cr2 & RTC_VDET_MASK) {
		printk(KERN_WARNING "***RTC BATTERY FAILURE***\n");
	}

	/* Handle century bit */
	if (rtc_tm->tm_mon & RTC_Y2K_MASK) {
		rtc_tm->tm_mon &= ~RTC_Y2K_MASK;
		rtc_tm->tm_year += 0x100;
	}

	rtc_tm->tm_sec  = from_bcd(rtc_tm->tm_sec);
	rtc_tm->tm_min  = from_bcd(rtc_tm->tm_min);
	rtc_tm->tm_hour = from_bcd(rtc_tm->tm_hour);
	rtc_tm->tm_mday = from_bcd(rtc_tm->tm_mday);
	rtc_tm->tm_mon  = from_bcd(rtc_tm->tm_mon) - 1;
	rtc_tm->tm_year = from_bcd(rtc_tm->tm_year);

	rtc_tm->tm_isdst = -1; /* DST not known */
}

static void set_rtc_time(struct rtc_time *rtc_tm)
{
	rtc_tm->tm_sec  = to_bcd(rtc_tm->tm_sec);
	rtc_tm->tm_min  = to_bcd(rtc_tm->tm_min);
	rtc_tm->tm_hour = to_bcd(rtc_tm->tm_hour);
	rtc_tm->tm_mday = to_bcd(rtc_tm->tm_mday);
	rtc_tm->tm_mon  = to_bcd(rtc_tm->tm_mon + 1);
	rtc_tm->tm_year = to_bcd(rtc_tm->tm_year);

	if (rtc_tm->tm_year >= 0x100) {
		rtc_tm->tm_year -= 0x100;
		rtc_tm->tm_mon |= RTC_Y2K_MASK;
	}

	i2c_start();
	i2c_outb(RTC_I2C_ADDRESS | I2C_WRITE_MASK);
	i2c_outb(0x00);	/* set starting register to 0 (=seconds) */
	i2c_outb(rtc_tm->tm_sec);
	i2c_outb(rtc_tm->tm_min);
	i2c_outb(rtc_tm->tm_hour);
	i2c_outb(rtc_tm->tm_wday);
	i2c_outb(rtc_tm->tm_mday);
	i2c_outb(rtc_tm->tm_mon);
	i2c_outb(rtc_tm->tm_year);
	i2c_stop();
}

static ssize_t rtc_write(UNUSED struct file *filp, const char *buf,
                         size_t count, loff_t *ppos)
{
	struct rtc_time rtc_tm;
	char buffer[23];
	char *p;

	if (!capable(CAP_SYS_TIME))
		return -EACCES;

	if (ppos != &filp->f_pos)
		return -ESPIPE;

	/*
	 * For simplicity, the only acceptable format is:
	 * YYYY:MM:DD:W:HH:MM:SS\n
	 */

	if (count != 22)
		goto err_out;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[sizeof(buffer)-1] = '\0';

	p = &buffer[0];

	rtc_tm.tm_year  = simple_strtoul(p, &p, 10);
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_mon = simple_strtoul(p, &p, 10) - 1;
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_mday = simple_strtoul(p, &p, 10);
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_wday = simple_strtoul(p, &p, 10);
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_hour = simple_strtoul(p, &p, 10);
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_min = simple_strtoul(p, &p, 10);
	if (*p++ != ':') goto err_out;

	rtc_tm.tm_sec = simple_strtoul(p, &p, 10);
	if (*p != '\n') goto err_out;

	rtc_tm.tm_year -= RTC_EPOCH;

	set_rtc_time(&rtc_tm);

	*ppos += count;

	return count;

 err_out:
	printk(KERN_ERR "invalid format: use YYYY:MM:DD:W:HH:MM:SS\\n\n");
	return -EINVAL;
}


static ssize_t rtc_read(UNUSED struct file *filp, char *buf, size_t count,
                        loff_t *ppos)
{
	char wbuf[23];
	struct rtc_time tm;
	ssize_t len;

	if (count == 0 || *ppos != 0)
		return 0;

	get_rtc_time(&tm);

	len = sprintf(wbuf, "%04d:%02d:%02d:%d:%02d:%02d:%02d\n",
		      tm.tm_year + RTC_EPOCH,
		      tm.tm_mon + 1,
		      tm.tm_mday,
		      tm.tm_wday,
		      tm.tm_hour,
		      tm.tm_min,
		      tm.tm_sec);

	if (len > (ssize_t)count)
		len = count;

	if (copy_to_user(buf, wbuf, len))
		return -EFAULT;

	*ppos += len;

	return len;
}

static int rtc_ioctl(UNUSED struct inode *inode, UNUSED struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	struct rtc_time rtc_tm;

	switch (cmd) {
		case RTC_RD_TIME:
			memset(&rtc_tm, 0, sizeof(struct rtc_time));
			get_rtc_time(&rtc_tm);
			if (copy_to_user((void *)arg, &rtc_tm, sizeof(rtc_tm)))
				return -EFAULT;
			break;

		case RTC_SET_TIME:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;

			if (copy_from_user(&rtc_tm, (struct rtc_time *)arg,
					   sizeof(struct rtc_time)))
				return -EFAULT;

			set_rtc_time(&rtc_tm);
			break;

		default:
			return -ENOTTY;
	}

	return 0;
}

static struct file_operations rtc_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = rtc_read,
	.write   = rtc_write,
	.ioctl   = rtc_ioctl,
	.open    = rtc_open,
	.release = rtc_release,
};

static struct miscdevice rtc_dev = {
	.minor = RTC_MINOR,
	.name  = "rtc",
	.fops  = &rtc_fops,
};

static int __init rtc_init(void)
{
	int cr1;

	if (!sda_mask || !scl_mask) {
		printk(KERN_ERR "gpiortc.o: you should specify sda_mask and scl_mask\n");
		return -ENODEV;
	}

	gpio_sbh = sb_kattach(SB_OSH);
	if (!gpio_sbh) {
		printk(KERN_ERR "gpiortc.o: sb_kattach() failed\n");
		return -ENODEV;
	}
	sb_gpiosetcore(gpio_sbh);

	i2c_init();

	/*
	 * Switch RTC to 24h mode
	 */
	i2c_start();
	i2c_outb(RTC_I2C_ADDRESS | I2C_WRITE_MASK);
	i2c_outb(0xE4); /* start at address 0xE, transmission mode 4 */
	cr1 = i2c_inb(I2C_NAK);
	i2c_stop();
	if ((cr1 & RTC_24HOUR_MODE_MASK) == 0) {
		/* RTC is running in 12h mode */
		printk(KERN_INFO "rtc.o: switching to 24h mode\n");
		i2c_start();
		i2c_outb(RTC_I2C_ADDRESS | I2C_WRITE_MASK);
		i2c_outb(0xE0);
		i2c_outb(cr1 | RTC_24HOUR_MODE_MASK);
		i2c_stop();
	}

	misc_register(&rtc_dev);

	printk(KERN_INFO "RV5C386A Real Time Clock Driver loaded\n");

	return 0;
}

static void __exit rtc_exit (void)
{
	misc_deregister(&rtc_dev);
	printk(KERN_INFO "Successfully removed RTC driver\n");
}

module_init(rtc_init);
module_exit(rtc_exit);

MODULE_PARM(rtc_debug, "i");
MODULE_PARM(sda_mask, "i");
MODULE_PARM(scl_mask, "i");

MODULE_AUTHOR("Andreas Engel");
MODULE_LICENSE("GPL");

/*
 * Local Variables:
 * indent-tabs-mode:t
 * c-basic-offset:8
 * End:
 */
