/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 *	note: only support mulititouch	Wenfs 2010-10-01
 *	
 *	modify for tiny210v2 by Victor Wen 2012-12
 */

#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "ft5x0x_ts.h"
#include <plat/ft5x0x_touch.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio.h>

/*
#define DEBUG 1
#define DEBUG_0 1
*/

#ifdef DEBUG_0
	#define TS_DEBUG(fmt,args...) printk(fmt, ##args )
	#else
	#define TS_DEBUG(fmt,args...)
#endif

#ifdef DEBUG
	#define TS_DEBUG1(fmt,args...) printk(fmt, ##args )
	#else
	#define TS_DEBUG1(fmt,args...)
#endif

static struct i2c_client *this_client;
static struct ft5x0x_ts_data *ft5x0x_ts;
static struct ft5x0x_i2c_platform_data *ft5x0x_pdata;

struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
	s16 touch_ID1;
	s16 touch_ID2;
	s16 touch_ID3;
	s16 touch_ID4;
	s16 touch_ID5;
    u8  touch_point;
};

struct ft5x0x_ts_data {
	struct input_dev	*input_dev;
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
};

static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	return ret;
}

static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

   	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}

static int ft5x0x_get_version()
{
	int ret;
	char rxdata = 0xa6;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= &rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	else
		ret = rxdata;
	
	return ret;
}
static int ft5x0x_set_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }
    
    return 0;
}

static void ft5x0x_ts_release(void)
{
	TS_DEBUG("ft5x0x_ts_release");
	input_mt_sync(ft5x0x_ts->input_dev);
	input_sync(ft5x0x_ts->input_dev);
}

static int ft5x0x_read_data(void)
{
	struct ts_event *event = &ft5x0x_ts->event;
//	u8 buf[14] = {0};
	u8 buf[32] = {0};
	int ret = -1;
	int status = 0;

//	ret = ft5x0x_i2c_rxdata(buf, 13);
	ret = ft5x0x_i2c_rxdata(buf, 31);
	if (ret < 0){
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}

	memset(event, 0, sizeof(struct ts_event));
//	event->touch_point = buf[2] & 0x03;// 0000 0011
	event->touch_point = buf[2] & 0x07;// 000 0111

    if (event->touch_point == 0) {
        ft5x0x_ts_release();
        return 1; 
    }

    switch (event->touch_point) {
		case 5:
			event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
			event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
			status = (s16)((buf[0x1b] & 0xc0) >> 6);
			event->touch_ID5=(s16)(buf[0x1D] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 4:
			event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
			event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
			status = (s16)((buf[0x15] & 0xc0) >> 6);
			event->touch_ID4=(s16)(buf[0x17] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 3:
			event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
			event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
			status = (s16)((buf[0x0f] & 0xc0) >> 6);
			event->touch_ID3=(s16)(buf[0x11] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
			status = (s16)((buf[0x9] & 0xc0) >> 6);
			event->touch_ID2=(s16)(buf[0x0b] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
			status = (s16)((buf[0x3] & 0xc0) >> 6);
			event->touch_ID1=(s16)(buf[0x05] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
            break;
		default:
		    return -1;
	}
    event->pressure = 200;

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);

    return 0;
}

static void ft5x0x_report_value(void)
{
	struct ts_event *event = &ft5x0x_ts->event;
	int i;
	u16 *points = (u16 *)event;
	int x, y;

		TS_DEBUG("==ft5x0x_report_value =\n");
	for (i = 0; i < event->touch_point; i++) {
		x = points[i + 1] * ft5x0x_pdata->screen_max_x / 1800;
		input_report_abs(ft5x0x_ts->input_dev, ABS_MT_POSITION_X, x);

		y = points[i] * ft5x0x_pdata->screen_max_y / 1024;
		input_report_abs(ft5x0x_ts->input_dev, ABS_MT_POSITION_Y, y);
		input_mt_sync(ft5x0x_ts->input_dev);
		input_sync(ft5x0x_ts->input_dev);
	}

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);
	TS_DEBUG1("1:(%d, %d) 2:(%d, %d) 3:(%d, %d) 4:(%d, %d) 5:(%d, %d)\n", 
		event->x1, event->y1, event->x2, event->y2, event->x3, event->y3,
		event->x4, event->y4, event->x5, event->y5);
}	/*end ft5x0x_report_value*/

static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;

//	disable_irq(this_client->irq);
	//disable_irq_nosync(this_client->irq);
//    	msleep(50);
	TS_DEBUG("------------------------------------\n");
	TS_DEBUG("ft5x0x_ts_pen_irq_work:START\n");
	ret = ft5x0x_read_data();
	if (ret == 0) {	
		ft5x0x_report_value();
	}
	TS_DEBUG("------------------------------------\n");
	TS_DEBUG("ft5x0x_ts_pen_irq_work:END\n");
    enable_irq(this_client->irq);
//	enable_irq(IRQ_EINT(6));
}

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x0x_ts_data *ft5x0x_ts = dev_id;

	TS_DEBUG("==int ft5x0x_ts_interrupt=\n");

	disable_irq_nosync(this_client->irq);
//	disable_irq(IRQ_EINT(6));


	if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
//	struct ft5x0x_ts_data *ts;
//	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);

	TS_DEBUG("==ft5x0x_ts_suspend=\n");
//	disable_irq(this_client->irq);
//	disable_irq(IRQ_EINT(6));
//	cancel_work_sync(&ts->pen_event_work);
//	flush_workqueue(ts->ts_workqueue);
	// ==set mode ==, 
//    	ft5x0x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
}

static void ft5x0x_ts_resume(struct early_suspend *handler)
{
	TS_DEBUG("==ft5x0x_ts_resume=\n");
	// wake the mode
//	__gpio_as_output(GPIO_FT5X0X_WAKE);		
//	__gpio_clear_pin(GPIO_FT5X0X_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_FT5X0X_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	enable_irq(this_client->irq);
//	enable_irq(IRQ_EINT(6));
}
#endif  //CONFIG_HAS_EARLYSUSPEND

static int 
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct input_dev *input_dev;
	int err = 0;

	int get_version;

	
	TS_DEBUG("\n\n\n\n#############################################==ft5x0x_ts_probe=\n");

	this_client = client;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ft5x0x_pdata = dev_get_platdata(&client->dev);
	/* config as eint */
	s3c_gpio_cfgpin(ft5x0x_pdata->gpio_irq, ft5x0x_pdata->irq_cfg);

	/* need ? */
	s3c_gpio_setpull(ft5x0x_pdata->gpio_irq, S3C_GPIO_PULL_NONE);
	this_client->irq = gpio_to_irq(ft5x0x_pdata->gpio_irq);

	TS_DEBUG("==kzalloc=\n");
	ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	TS_DEBUG("==INIT_WORK=\n");
	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	TS_DEBUG("==create_singlethread_workqueue=\n");
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

//	pdata = client->dev.platform_data;
//	if (pdata == NULL) {
//		dev_err(&client->dev, "%s: platform data is null\n", __func__);
//		goto exit_platform_data_null;
//	}
	
//	TS_DEBUG("==request_irq=\n");
//	err = request_irq(client->irq, ft5x0x_ts_interrupt, IRQF_DISABLED, "ft5x0x_ts", ft5x0x_ts);
//	err = request_irq(IRQ_EINT(6), ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", ft5x0x_ts);

	err = request_irq(this_client->irq, ft5x0x_ts_interrupt,
			IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			"ft5x0x_ts", ft5x0x_ts);
	if (err < 0) {
		dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

//	__gpio_as_irq_fall_edge(pdata->intr);		//

	TS_DEBUG("==input_allocate_device=\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	ft5x0x_ts->input_dev = input_dev;

	TS_DEBUG("FT5X0X: screen_max_x = %d\n", ft5x0x_pdata->screen_max_x);
	TS_DEBUG("FT5X0X: screen_max_y = %d\n", ft5x0x_pdata->screen_max_y);
	TS_DEBUG("FT5X0X: pressure_max = %d\n", ft5x0x_pdata->pressure_max);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, ft5x0x_pdata->screen_max_x,
				0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, ft5x0x_pdata->screen_max_y,
				0, 0);

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_SYN, input_dev->evbit);

	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"ft5x0x_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	TS_DEBUG("==register_early_suspend =\n");
	ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
	ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
	register_early_suspend(&ft5x0x_ts->early_suspend);
#endif
//wake the CTPM
//	__gpio_as_output(GPIO_FT5X0X_WAKE);		
//	__gpio_clear_pin(GPIO_FT5X0X_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_FT5X0X_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	ft5x0x_set_reg(0x88, 0x05); //5, 6,7,8
//	ft5x0x_set_reg(0x80, 30);
//	msleep(50);

//	enable_irq(this_client->irq);
//	enable_irq(IRQ_EINT(6));
//
	get_version = ft5x0x_get_version();

	printk(KERN_INFO "tx5x0x Firmware version:%d\n", get_version);

	TS_DEBUG("==probe over =, this_client->irq=%d\n\n\n", this_client->irq);
    return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(client->irq, ft5x0x_ts);
//	free_irq(IRQ_EINT(6), ft5x0x_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
	TS_DEBUG("==singlethread error =\n\n\n");
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int __devexit ft5x0x_ts_remove(struct i2c_client *client)
{
	TS_DEBUG("==ft5x0x_ts_remove=\n");
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
	free_irq(client->irq, ft5x0x_ts);
//	free_irq(IRQ_EINT(6), ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{ FT5X0X_NAME, 0 },{ }
};
MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= __devexit_p(ft5x0x_ts_remove),
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init ft5x0x_ts_init(void)
{
	return i2c_add_driver(&ft5x0x_ts_driver);
}

static void __exit ft5x0x_ts_exit(void)
{
	i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");


