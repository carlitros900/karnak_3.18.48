/*
* Copyright(C)2014 MediaTek Inc.
* Modification based on code covered by the below mentioned copyright
* and/or permission notice(S).
*/

/*
 * Author: MingHsien Hsieh <minghsien.hsieh@mediatek.com>
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
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
#endif
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/wakelock.h>
#include <asm/io.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <hwmsen_helper.h>
#include <hwmsensor.h>
#include <sensors_io.h>
#include <hwmsen_dev.h>
#include <cust_alsps.h>
#include <alsps.h>
#include "jsa1214.h"

#include <upmu_hw.h>
#include <upmu_sw.h>
#include <upmu_common.h>

#define DRIVER_VERSION			"3.1.2.1nk"
#include <mt_typedefs.h>
#include <mt_gpio.h>
/*------------------------- define-------------------------------*/
struct alsps_hw alsps_cust;
static struct alsps_hw *hw = &alsps_cust;
#define POWER_NONE_MACRO MT65XX_POWER_NONE

#if defined(STK3X1_TILT_FUNC)
	#define GPIO_ALS_EINT_PIN  1	  /*eint gpio pin num*/
	#define GPIO_ALS_EINT_PIN_M_EINT	2/*eint mode*/
	#define CUST_EINT_ALS_NUM		 3/*eint num*/
	#define CUST_EINT_ALS_DEBOUNCE_CN  4/*debounce time*/
	#define CUST_EINT_ALS_TYPE	 5 /*eint trigger type*/
	#define CUST_EINTF_TRIGGER_LOW 6

	#define CUST_EINT_ALS_SENSITIVE		CUST_EINTF_TRIGGER_LOW
	#define CUST_EINT_ALS_POLARITY		CUST_EINT_ALS_TYPE
	#define POWER_NONE_MACRO MT65XX_POWER_NONE
	#define CUST_EINT_STK3X1X_NUM CUST_EINT_ALS_NUM
	#define GPIO_STK3X1X_EINT_PIN GPIO_ALS_EINT_PIN
#endif

unsigned int cci_als_value;
unsigned int cci_als_value_cali = 0;
unsigned int cci_transmittance_cali = 0;
unsigned int als_cal = 0;
#define STK3X1X_CAL_FILE   "/data/als_cal_data.bin"
#define STK3x1x_DATA_BUF_NUM 1
#define Default_cali 765;

static struct of_device_id stk3x1x_dts_table[] = {
	{ .compatible  = "mediatek,alsps",},
	{},
};

/******************************************************************************
 * configuration
*******************************************************************************/
#define PSCTRL_VAL	0x71	/* ps_persistance=4, ps_gain=64X, PS_IT=0.391ms */
#define ALSCTRL_VAL	0x39	/* als_persistance=1, als_gain=64X, ALS_IT=50ms */
#define LEDCTRL_VAL	0xFF	/* 100mA IRDR, 64/64 LED duty */
#define WAIT_VAL		0x09		/* 50 ms */

/*----------------------------------------------------------------------------*/
#define stk3x1x_DEV_NAME	 "stk3x1x"
/*----------------------------------------------------------------------------*/
#define APS_TAG					 "[ALS/PS] "
#define APS_FUN(f)				 printk(APS_TAG"%s\n", __FUNCTION__)
#define APS_ERR(fmt, args...)	 printk(APS_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define APS_LOG(fmt, args...)	 printk(APS_TAG fmt, ##args)
#define APS_DBG(fmt, args...)	 printk(fmt, ##args)
/******************************************************************************
 * extern functions
*******************************************************************************/
/*----------------------------------------------------------------------------*/
extern void mt_eint_unmask(unsigned int line);
extern void mt_eint_mask(unsigned int line);
extern void mt_eint_set_hw_debounce(unsigned int eint_num, unsigned int ms);
extern unsigned int mt_eint_set_sens(unsigned int eint_num, unsigned int sens);
extern void mt_eint_registration(unsigned int eint_num, unsigned int flag, void (EINT_FUNC_PTR) (void), unsigned int is_auto_umask);

/*----------------------------------------------------------------------------*/
#define STK2213_PID			0x23
#define STK2213I_PID			0x22
#define STK3010_PID			0x33
#define STK3210_STK3310_PID	0x13
#define STK3211_STK3311_PID	0x12
#define STK3311_WV_PID				 0x1D

#define STK_IRC_MAX_ALS_CODE		20000
#define STK_IRC_MIN_ALS_CODE		25
#define STK_IRC_MIN_IR_CODE		50
#define STK_IRC_ALS_DENOMI		2
#define STK_IRC_ALS_NUMERA		5
#define STK_IRC_ALS_CORREC		748

/*----------------------------------------------------------------------------*/
static struct i2c_client *stk3x1x_i2c_client;
/*----------------------------------------------------------------------------*/
static const struct i2c_device_id stk3x1x_i2c_id[] = { {stk3x1x_DEV_NAME, 0}, {} };
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#else
/*the adapter id & i2c address will be available in customization*/
static unsigned short stk3x1x_force[] = {0x00, 0x00, I2C_CLIENT_END, I2C_CLIENT_END};
static const unsigned short *const stk3x1x_forces[] = { stk3x1x_force, NULL};
static struct i2c_client_address_data stk3x1x_addr_data = { .forces = stk3x1x_forces,};
#endif
/*----------------------------------------------------------------------------*/
static int stk3x1x_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int stk3x1x_i2c_remove(struct i2c_client *client);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
static int stk3x1x_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
#endif
/*----------------------------------------------------------------------------*/
static int stk3x1x_i2c_suspend(struct i2c_client *client, pm_message_t msg);
static int stk3x1x_i2c_resume(struct i2c_client *client);
static struct stk3x1x_priv *g_stk3x1x_ptr;
static unsigned long long int_top_time;
#define C_I2C_FIFO_SIZE		8

static DEFINE_MUTEX(STK3X1X_i2c_mutex);
static int  stk3x1x_init_flag = -1;
static int  stk3x1x_local_init(void);
static int  stk3x1x_local_uninit(void);
static struct alsps_init_info stk3x1x_init_info = {
	.name = "stk3x1x",
	.init = stk3x1x_local_init,
	.uninit = stk3x1x_local_uninit,

};

/*----------------------------------------------------------------------------*/
typedef enum {
	STK_TRC_ALS_DATA = 0x0001,
	STK_TRC_PS_DATA = 0x0002,
	STK_TRC_EINT    = 0x0004,
	STK_TRC_IOCTL   = 0x0008,
	STK_TRC_I2C     = 0x0010,
	STK_TRC_CVT_ALS = 0x0020,
	STK_TRC_CVT_PS  = 0x0040,
	STK_TRC_DEBUG   = 0x8000,
} STK_TRC;
/*----------------------------------------------------------------------------*/
typedef enum {
	STK_BIT_ALS    = 1,
	STK_BIT_PS     = 2,
} STK_BIT;
/*----------------------------------------------------------------------------*/
struct stk3x1x_i2c_addr {
/*define a series of i2c slave address*/
	u8  state;			/* enable/disable state */
	u8  psctrl;			/* PS control */
	u8  alsctrl;		/* ALS control */
	u8  ledctrl;		/* LED control */
	u8  intmode;		/* INT mode */
	u8  wait;			/* wait time */
	u8  thdh1_ps;		/* PS INT threshold high 1 */
	u8  thdh2_ps;		/* PS INT threshold high 2 */
	u8  thdl1_ps;		/* PS INT threshold low 1 */
	u8  thdl2_ps;		/* PS INT threshold low 2 */
	u8  thdh1_als;		/* ALS INT threshold high 1 */
	u8  thdh2_als;		/* ALS INT threshold high 2 */
	u8  thdl1_als;		/* ALS INT threshold low 1 */
	u8  thdl2_als;		/* ALS INT threshold low 2 */
	u8  flag;			/* int flag */
	u8  data1_ps;		/* ps data1 */
	u8  data2_ps;		/* ps data2 */
	u8  data1_als;		/* als data1 */
	u8  data2_als;		/* als data2 */
	u8  data1_offset;	/* offset data1 */
	u8  data2_offset;	/* offset data2 */
	u8  data1_ir;		/* ir data1 */
	u8  data2_ir;		/* ir data2 */
	u8  soft_reset;		/* software reset */
};
/*----------------------------------------------------------------------------*/
#ifdef STK_FIR
struct data_filter {
	s16 raw[8];
	int sum;
	int num;
	int idx;
};
#endif

struct stk3x1x_priv {
	struct alsps_hw  *hw;
	struct i2c_client *client;
	struct delayed_work  eint_work;

	/*i2c address group*/
	struct stk3x1x_i2c_addr  addr;

	/*misc*/
	atomic_t    trace;
	atomic_t    i2c_retry;
	atomic_t    als_suspend;
	atomic_t    als_debounce;	/*debounce time after enabling als*/
	atomic_t    als_deb_on;		/*indicates if the debounce is on*/
	atomic_t    als_deb_end;	/*the jiffies representing the end of debounce*/
	atomic_t    ps_mask;		/*mask ps: always return far away*/
	atomic_t    ps_debounce;	/*debounce time after enabling ps*/
	atomic_t    ps_deb_on;		/*indicates if the debounce is on*/
	atomic_t    ps_deb_end;		/*the jiffies representing the end of debounce*/
	atomic_t    ps_suspend;


	/*data*/
	u16         als;
	u16         ps;
	u8          _align;
	u16         als_level_num;
	u16         als_value_num;
	u32         als_level[C_CUST_ALS_LEVEL-1];
	u32         als_value[C_CUST_ALS_LEVEL];
	int     ps_cali;

	atomic_t    state_val;
	atomic_t    psctrl_val;
	atomic_t    alsctrl_val;
	u8          wait_val;
	u8          ledctrl_val;
	u8          int_val;

	atomic_t    ps_high_thd_val;	 /*the cmd value can't be read, stored in ram*/
	atomic_t    ps_low_thd_val;		/*the cmd value can't be read, stored in ram*/
	ulong       enable;			/*enable mask*/
	ulong       pending_intr;	/*pending interrupt*/
	atomic_t    recv_reg;
	/*early suspend*/
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend    early_drv;
#endif
	bool first_boot;
#ifdef STK_FIR
	struct data_filter      fir;
#endif
	uint16_t ir_code;
	uint16_t als_correct_factor;
	struct mutex io_lock;
	uint32_t als_transmittance;
	bool als_enabled;
	int32_t als_lux_last;
};
/*----------------------------------------------------------------------------*/
static struct i2c_driver stk3x1x_i2c_driver = {
	.probe      = stk3x1x_i2c_probe,
	.remove     = stk3x1x_i2c_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
	.detect     = stk3x1x_i2c_detect,
#endif
	.suspend    = stk3x1x_i2c_suspend,
	.resume     = stk3x1x_i2c_resume,
	.id_table   = stk3x1x_i2c_id,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
	.address_data = &stk3x1x_addr_data,
#endif
	.driver = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
		.owner          = THIS_MODULE,
#endif
		.name           = stk3x1x_DEV_NAME,
		.of_match_table = stk3x1x_dts_table,
	},
};



static struct stk3x1x_priv *stk3x1x_obj;
static int stk3x1x_get_ps_value(struct stk3x1x_priv *obj, u16 ps);
static int stk3x1x_get_ps_value_only(struct stk3x1x_priv *obj, u16 ps);
static int stk3x1x_get_als_value(struct stk3x1x_priv *obj, u16 als);
static int stk3x1x_read_als(struct i2c_client *client, u16 *data);
static int stk3x1x_read_ps(struct i2c_client *client, u16 *data);
static int stk3x1x_set_als_int_thd(struct i2c_client *client, u16 als_data_reg);
static int32_t stk3x1x_get_ir_value(struct stk3x1x_priv *obj);
struct wake_lock ps_lock;

/*----------------------------------------------------------------------------*/
int stk3x1x_get_addr(struct alsps_hw *hw, struct stk3x1x_i2c_addr *addr)
{
	if (!hw || !addr) {
		return -EFAULT;
	}
	/*
	addr->state   = STK_STATE_REG;
	addr->psctrl   = STK_PSCTRL_REG;
	addr->alsctrl  = STK_ALSCTRL_REG;
	addr->ledctrl  = STK_LEDCTRL_REG;
	addr->intmode	 = STK_INT_REG;
	addr->wait	  = STK_WAIT_REG;
	addr->thdh1_ps	  = STK_THDH1_PS_REG;
	addr->thdh2_ps	  = STK_THDH2_PS_REG;
	addr->thdl1_ps = STK_THDL1_PS_REG;
	addr->thdl2_ps = STK_THDL2_PS_REG;
	addr->thdh1_als    = STK_THDH1_ALS_REG;
	addr->thdh2_als    = STK_THDH2_ALS_REG;
	addr->thdl1_als = STK_THDL1_ALS_REG ;
	addr->thdl2_als = STK_THDL2_ALS_REG;
	addr->flag = STK_FLAG_REG;
	addr->data1_ps = STK_DATA1_PS_REG;
	addr->data2_ps = STK_DATA2_PS_REG;
	addr->data1_als = STK_DATA1_ALS_REG;
	addr->data2_als = STK_DATA2_ALS_REG;
	addr->data1_offset = STK_DATA1_OFFSET_REG;
	addr->data2_offset = STK_DATA2_OFFSET_REG;
	addr->data1_ir = STK_DATA1_IR_REG;
	addr->data2_ir = STK_DATA2_IR_REG;
	addr->soft_reset = STK_SW_RESET_REG;
	*/
	addr->state   = STK_STATE_REG;
	addr->psctrl   = STK_PSCTRL_REG;
	addr->alsctrl  = STK_ALSCTRL_REG;
	addr->ledctrl  = STK_LEDCTRL_REG;
	addr->intmode    = STK_INT_REG;
	addr->wait    = STK_WAIT_REG;
	addr->thdh1_ps    = STK_THDH1_PS_REG;
	addr->thdh2_ps    = STK_THDH2_PS_REG;
	addr->thdl1_ps = STK_THDL1_PS_REG;
	addr->thdl2_ps = STK_THDL2_PS_REG;
	addr->thdh1_als    = STK_THDH1_ALS_REG;
	addr->thdh2_als    = STK_THDH2_ALS_REG;
	addr->thdl1_als = STK_THDL1_ALS_REG ;
	addr->thdl2_als = STK_THDL2_ALS_REG;
	addr->flag = STK_FLAG_REG;
	addr->data1_ps = STK_DATA1_PS_REG;
	addr->data2_ps = STK_DATA2_PS_REG;
	addr->data1_als = STK_DATA1_ALS_REG;
	addr->data2_als = STK_DATA2_ALS_REG;
	addr->data1_offset = STK_DATA1_OFFSET_REG;
	addr->data2_offset = STK_DATA2_OFFSET_REG;
	addr->data1_ir = STK_DATA1_IR_REG;
	addr->data2_ir = STK_DATA2_IR_REG;
	addr->soft_reset = STK_SW_RESET_REG;
	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_hwmsen_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
	int err;
	u8 beg = addr;

	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &beg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		}
	};
	mutex_lock(&STK3X1X_i2c_mutex);

	if (!client) {
		mutex_unlock(&STK3X1X_i2c_mutex);
		return -EINVAL;
	} else if (len > C_I2C_FIFO_SIZE) {
		mutex_unlock(&STK3X1X_i2c_mutex);
		APS_LOG(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}

	err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
	mutex_unlock(&STK3X1X_i2c_mutex);
	if (err != 2) {
		APS_LOG("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
		err = -EIO;
	} else {
		err = 0;/*no error*/
	}
	return err;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_get_timing(void)
{
	return 200;
/*
	u32 base = I2C2_BASE;
	return (__raw_readw(mt6516_I2C_HS) << 16) | (__raw_readw(mt6516_I2C_TIMING));
*/
}

/*----------------------------------------------------------------------------*/
int stk3x1x_master_recv(struct i2c_client *client, u16 addr, u8 *buf , int count)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0, retry = 0;
	int trc = atomic_read(&obj->trace);
	int max_try = atomic_read(&obj->i2c_retry);

	while (retry++ < max_try) {
		ret = stk3x1x_hwmsen_read_block(client, addr, buf, count);
		if (ret == 0)
			break;
		udelay(100);
	}

	if (unlikely(trc)) {
		if ((retry != 1) && (trc & STK_TRC_DEBUG)) {
			APS_LOG("(recv) %d/%d\n", retry-1, max_try);
		}
	}
	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	transmitted, else error code. */
	return (ret == 0) ? count : ret;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_master_send(struct i2c_client *client, u16 addr, u8 *buf, int count)
{
	int ret = 0, retry = 0;
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int trc = atomic_read(&obj->trace);
	int max_try = atomic_read(&obj->i2c_retry);


	while (retry++ < max_try) {
		ret = hwmsen_write_block(client, addr, buf, count);
		if (ret == 0)
			break;
		udelay(100);
	}

	if (unlikely(trc)) {
		if ((retry != 1) && (trc & STK_TRC_DEBUG)) {
			APS_LOG("(send) %d/%d\n", retry-1, max_try);
		}
	}
	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	transmitted, else error code. */
	return (ret == 0) ? count : ret;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_led(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.ledctrl, &data, 1);
	if (ret < 0) {
		APS_ERR("write led = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_read_als(struct i2c_client *client, u16 *data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;
	u8 buf[2];
	u16 als_data;
#ifdef STK_FIR
	int idx;
#endif
	if (NULL == client) {
		return -EINVAL;
	}
	ret = stk3x1x_master_recv(client, 0x09, buf, 0x02);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	} else {
		als_data = ((buf[1]&0x0F) << 8) | (buf[0]);

	}

	*data = als_data;

	if (atomic_read(&obj->trace) & STK_TRC_ALS_DATA) {
		APS_DBG("ALS: 0x%04X\n", (u32)(*data));
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_als(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.alsctrl, &data, 1);
	if (ret < 0) {
		APS_ERR("write als = %d\n", ret);
		return -EFAULT;
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_read_flag(struct i2c_client *client, u8 *data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;
	u8 buf;

	if (NULL == client) {
		return -EINVAL;
	}
	ret = stk3x1x_master_recv(client, obj->addr.flag, &buf, 0x01);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	} else {
		*data = buf;
	}

	if (atomic_read(&obj->trace) & STK_TRC_ALS_DATA) {
		APS_DBG("PS NF flag: 0x%04X\n", (u32)(*data));
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_read_id(struct i2c_client *client)
{
	int ret = 0;
	u8 buf[2];

	if (NULL == client) {
		return -EINVAL;
	}
	ret = stk3x1x_master_recv(client, STK_PDT_ID_REG, buf, 0x02);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	APS_LOG("%s: PID=0x%d, VID=0x%x\n", __func__, buf[0], buf[1]);

	if (buf[1] == 0xC0)
		APS_LOG("%s: RID=0xC0!!!!!!!!!!!!!\n", __func__);

	switch (buf[0]) {
	case STK2213_PID:
	case STK2213I_PID:
	case STK3010_PID:
	case STK3210_STK3310_PID:
	case STK3211_STK3311_PID:
		return 0;
	case 0x0:
		APS_ERR("PID=0x0, please make sure the chip is stk3x1x!\n");
		return -2;
	default:
		APS_ERR("%s: invalid PID(%#x)\n", __func__, buf[0]);
		return -1;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_read_ps(struct i2c_client *client, u16 *data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;
	u8 buf[2];

	if (NULL == client) {
		APS_ERR("i2c client is NULL\n");
		return -EINVAL;
	}
	ret = stk3x1x_master_recv(client, obj->addr.data1_ps, buf, 0x02);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	} else {
		if (((buf[0] << 8) | (buf[1])) < obj->ps_cali)
			*data = 0;
		else
			*data = ((buf[0] << 8) | (buf[1])) - obj->ps_cali;
	}

	if (atomic_read(&obj->trace) & STK_TRC_ALS_DATA) {
		APS_DBG("PS: 0x%04X\n", (u32)(*data));
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_ps(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.psctrl, &data, 1);
	if (ret < 0) {
		APS_ERR("write ps = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_write_wait(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.wait, &data, 1);
	if (ret < 0) {
		APS_ERR("write wait = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_write_int(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.intmode, &data, 1);
	if (ret < 0) {
		APS_ERR("write intmode = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_write_state(struct i2c_client *client, u8 data)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int ret = 0;

	ret = stk3x1x_master_send(client, obj->addr.state, &data, 1);
	if (ret < 0) {
		APS_ERR("write state = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_flag(struct i2c_client *client, u8 data)
{
	int ret = 0;
	ret = stk3x1x_master_send(client, 0x02, 0, 1);
	if (ret < 0) {
		APS_ERR("write ps = %d\n", ret);
		return -EFAULT;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_sw_reset(struct i2c_client *client)
{
	u8 buf[2];
	int ret = 0;

	buf[0] = 0x29;
	buf[1] = 0x00;

	ret = stk3x1x_master_send(client, 0x01, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, 0x0F, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, 0x0E, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, 0x0F, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	msleep(1);
	return 0;
}

/*----------------------------------------------------------------------------*/
int stk3x1x_write_ps_high_thd(struct i2c_client *client, u16 thd)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	u8 buf[2];
	int ret = 0;

	buf[0] = (u8) ((0xFF00 & thd) >> 8);
	buf[1] = (u8) (0x00FF & thd);
	ret = stk3x1x_master_send(client, obj->addr.thdh1_ps, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("WARNING: %d\n",  ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, obj->addr.thdh2_ps, &(buf[1]), 1);
	if (ret < 0) {
		APS_ERR("WARNING: %d\n", ret);
		return -EFAULT;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_ps_low_thd(struct i2c_client *client, u16 thd)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	u8 buf[2];
	int ret = 0;

	buf[0] = (u8) ((0xFF00 & thd) >> 8);
	buf[1] = (u8) (0x00FF & thd);
	ret = stk3x1x_master_send(client, obj->addr.thdl1_ps, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, obj->addr.thdl2_ps, &(buf[1]), 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_als_high_thd(struct i2c_client *client, u16 thd)
{
	u8 buf[2];
	int ret = 0;

	buf[0] = (u8) ((0x00F & thd) << 4);
	buf[1] = (u8) ((0xFF0 & thd)>>4);
	ret = stk3x1x_master_send(client, 0x06, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, 0x07, &(buf[1]), 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_write_als_low_thd(struct i2c_client *client, u16 thd)
{
	u8 buf[2], temp;
	int ret = 0;

	buf[0] = (u8) ((0x0FF & thd));
	buf[1] = (u8) ((0xF00 & thd)>>8);

	ret = stk3x1x_master_send(client, 0x05, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_recv(client, 0x06, &temp, 0x01);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}

	buf[1] = buf[1]|(temp&0xF0);

	ret = stk3x1x_master_send(client, 0x06, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("WARNING: %s: %d\n", __func__, ret);
		return -EFAULT;
	}

	return 0;
}

bool hwPowerOn2(MT65XX_POWER powerId, int voltage_uv, char *mode_name)
{
	return true;
}

bool hwPowerDown2(MT65XX_POWER powerId, char *mode_name)
{
	return true;
}

static void stk3x1x_power(struct alsps_hw *hw, unsigned int on)
{
	static unsigned int power_on;

	if (hw->power_id != POWER_NONE_MACRO) {
		if (power_on == on) {
			APS_LOG("ignore power control: %d\n", on);
		} else if (on) {
			if (!hwPowerOn2(hw->power_id, hw->power_vol, "stk3x1x")) {
				APS_ERR("power on fails!!\n");
			}
		} else {
			if (!hwPowerDown2(hw->power_id, "stk3x1x")) {
				APS_ERR("power off fail!!\n");
			}
		}
	}
	power_on = on;
}

/*----------------------------------------------------------------------------*/
static int stk3x1x_enable_als(struct i2c_client *client, int enable)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int err, cur = 0;
	int trc = atomic_read(&obj->trace);

	APS_LOG("%s: enable=%d\n", __func__, enable);

	if (enable && obj->hw->polling_mode_als == 0) {
		stk3x1x_write_als_high_thd(client, 0x0);
		stk3x1x_write_als_low_thd(client, 0xFFFF);
	}

	err = stk3x1x_write_state(client, cur);
	if (err < 0)
		return err;
	else
		atomic_set(&obj->state_val, cur);

	if (enable) {
		if (obj->hw->polling_mode_als) {
			atomic_set(&obj->als_deb_on, 1);
			atomic_set(&obj->als_deb_end, jiffies+atomic_read(&obj->als_debounce)*HZ/1000);
		} else {
			schedule_delayed_work(&obj->eint_work, 220*HZ/1000);
		}
	}

	if (trc & STK_TRC_DEBUG) {
		APS_LOG("enable als (%d)\n", enable);
	}

	return err;
}
/*----------------------------------------------------------------------------*/
static int stk3x1x_enable_ps(struct i2c_client *client, int enable)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int err, cur = 0, old = atomic_read(&obj->state_val);
	int trc = atomic_read(&obj->trace);
	struct hwm_sensor_data sensor_data;
	return 0;

	cur = old;

	if (obj->first_boot == true) {
		obj->first_boot = false;

		err = stk3x1x_write_ps_high_thd(client, atomic_read(&obj->ps_high_thd_val));
		if (err) {
			APS_ERR("write high thd error: %d\n", err);
			return err;
		}

		err = stk3x1x_write_ps_low_thd(client, atomic_read(&obj->ps_low_thd_val));
		if (err) {
			APS_ERR("write low thd error: %d\n", err);
			return err;
		}
	}

	APS_LOG("%s: enable=%d\n", __FUNCTION__, enable);
	cur &= (~(0x45));
	if (enable) {
		cur |= (STK_STATE_EN_PS_MASK);
		if (!(old & STK_STATE_EN_ALS_MASK))
			cur |= STK_STATE_EN_WAIT_MASK;
		if (1 == obj->hw->polling_mode_ps)
			wake_lock(&ps_lock);
	} else {
		if (1 == obj->hw->polling_mode_ps)
			wake_unlock(&ps_lock);
	}

	if (trc & STK_TRC_DEBUG) {
		APS_LOG("%s: %08X, %08X, %d\n", __func__, cur, old, enable);
	}

	if (0 == (cur ^ old)) {
		return 0;
	}

	err = stk3x1x_write_state(client, cur);
	if (err < 0)
		return err;
	else
		atomic_set(&obj->state_val, cur);

	if (enable) {
		if (obj->hw->polling_mode_ps) {
			atomic_set(&obj->ps_deb_on, 1);
			atomic_set(&obj->ps_deb_end, jiffies+atomic_read(&obj->ps_debounce)*HZ/1000);
		} else {
			msleep(4);
			err = stk3x1x_read_ps(obj->client, &obj->ps);
			if (err) {
				APS_ERR("stk3x1x read ps data: %d\n", err);
				return err;
			}

			err = stk3x1x_get_ps_value_only(obj, obj->ps);
			if (err < 0) {
				APS_ERR("stk3x1x get ps value: %d\n", err);
				return err;
			} else if (stk3x1x_obj->hw->polling_mode_ps == 0) {
				sensor_data.values[0] = err;
				sensor_data.value_divide = 1;
				sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
				APS_LOG("%s:ps raw 0x%x -> value 0x%x \n", __FUNCTION__, obj->ps, sensor_data.values[0]);
				if (ps_report_interrupt_data(sensor_data.values[0])) {
					APS_ERR("call ps_report_interrupt_data fail\n");
				}
			}
		}
	}

	if (trc & STK_TRC_DEBUG) {
		APS_LOG("enable ps	(%d)\n", enable);
	}

	return err;
}
/*----------------------------------------------------------------------------*/

static int stk3x1x_check_intr(struct i2c_client *client, u8 *status)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int err;

	err = stk3x1x_read_flag(client, status);
	if (err < 0) {
		APS_ERR("WARNING: read flag reg error: %d\n", err);
		return -EFAULT;
	}
	APS_LOG("%s: read status reg: 0x%x\n", __func__, *status);

	if (*status & STK_FLG_ALSINT_MASK) {
		set_bit(STK_BIT_ALS, &obj->pending_intr);
	} else {
		clear_bit(STK_BIT_ALS, &obj->pending_intr);
	}

	if (*status & STK_FLG_PSINT_MASK) {
		set_bit(STK_BIT_PS,  &obj->pending_intr);
	} else {
		clear_bit(STK_BIT_PS, &obj->pending_intr);
	}

	if (atomic_read(&obj->trace) & STK_TRC_DEBUG) {
		APS_LOG("check intr: 0x%02X => 0x%08lX\n", *status, obj->pending_intr);
	}

	return 0;
}

static int stk3x1x_clear_intr(struct i2c_client *client, u8 status, u8 disable_flag)
{
	int err = 0;

	status = status | (STK_FLG_ALSINT_MASK | STK_FLG_PSINT_MASK | STK_FLG_OUI_MASK | STK_FLG_IR_RDY_MASK);
	status &= (~disable_flag);
	APS_LOG(" set flag reg: 0x%x\n", status);
	err = stk3x1x_write_flag(client, status);
	if (err)
		APS_ERR("stk3x1x_write_flag failed, err=%d\n", err);
	return err;
}

/*----------------------------------------------------------------------------*/
static int stk3x1x_set_als_int_thd(struct i2c_client *client, u16 als_data_reg)
{
	s32 als_thd_h, als_thd_l;

	als_thd_h = als_data_reg + STK_ALS_CODE_CHANGE_THD;
	als_thd_l = als_data_reg - STK_ALS_CODE_CHANGE_THD;
	if (als_thd_h >= (1 << 16))
		als_thd_h = (1 << 16) - 1;
	if (als_thd_l < 0)
		als_thd_l = 0;
	APS_LOG("stk3x1x_set_als_int_thd:als_thd_h:%d,als_thd_l:%d\n", als_thd_h, als_thd_l);

	stk3x1x_write_als_high_thd(client, als_thd_h);
	stk3x1x_write_als_low_thd(client, als_thd_l);

	return 0;
}

/*----------------------------------------------------------------------------*/

void stk3x1x_eint_func(void)
{
	struct stk3x1x_priv *obj = g_stk3x1x_ptr;
	APS_LOG(" interrupt fuc\n");
	if (!obj) {
		return;
	}
	int_top_time = sched_clock();
	if (obj->hw->polling_mode_ps == 0 || obj->hw->polling_mode_als == 0)
		schedule_delayed_work(&obj->eint_work, 0);
	if (atomic_read(&obj->trace) & STK_TRC_EINT) {
		APS_LOG("eint: als/ps intrs\n");
	}
}

/*----------------------------------------------------------------------------*/
static void stk3x1x_eint_work(struct work_struct *work)
{
	struct stk3x1x_priv *obj = g_stk3x1x_ptr;
	int err;
	struct hwm_sensor_data sensor_data;
	u8 flag_reg, disable_flag = 0;

	memset(&sensor_data, 0, sizeof(sensor_data));

	APS_LOG("stk3x1x int top half time = %lld\n", int_top_time);

	err = stk3x1x_check_intr(obj->client, &flag_reg);
	if (err) {
		APS_ERR("stk3x1x_check_intr fail: %d\n", err);
		goto err_i2c_rw;
	}

	APS_LOG(" &obj->pending_intr =%lx\n", obj->pending_intr);

	if (((1<<STK_BIT_ALS) & obj->pending_intr) && (obj->hw->polling_mode_als == 0)) {
		APS_LOG("stk als change\n");
		disable_flag |= STK_FLG_ALSINT_MASK;
		err = stk3x1x_read_als(obj->client, &obj->als);
		if (err) {
			APS_ERR("stk3x1x_read_als failed %d\n", err);
			goto err_i2c_rw;
		}

		stk3x1x_set_als_int_thd(obj->client, obj->als);
		sensor_data.values[0] = stk3x1x_get_als_value(obj, obj->als);
		sensor_data.value_divide = 1;
		sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
		APS_LOG("%s:als raw 0x%x -> value 0x%x \n", __FUNCTION__, obj->als, sensor_data.values[0]);
		if (ps_report_interrupt_data(sensor_data.values[0])) {
			APS_ERR("call ps_report_interrupt_data fail \n");
		}
	}
	if (((1<<STK_BIT_PS) &  obj->pending_intr) && (obj->hw->polling_mode_ps == 0)) {
		APS_LOG("stk ps change\n");
		disable_flag |= STK_FLG_PSINT_MASK;

		err = stk3x1x_read_ps(obj->client, &obj->ps);
		if (err) {
			APS_ERR("stk3x1x read ps data: %d\n", err);
			goto err_i2c_rw;
		}

		sensor_data.values[0] = (flag_reg & STK_FLG_NF_MASK) ? 1 : 0;
		sensor_data.value_divide = 1;
		sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
		APS_LOG("%s:ps raw 0x%x -> value 0x%x \n", __FUNCTION__, obj->ps, sensor_data.values[0]);
		if (ps_report_interrupt_data(sensor_data.values[0])) {
			APS_ERR("call ps_report_interrupt_data fail\n");
		}

	}

	err = stk3x1x_clear_intr(obj->client, flag_reg, disable_flag);
	if (err) {
		APS_ERR("fail: %d\n", err);
		goto err_i2c_rw;
	}

	msleep(1);
	return;

	err_i2c_rw:
	msleep(30);
	return;
}
/*----------------------------------------------------------------------------*/
int stk3x1x_setup_eint(struct i2c_client *client)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);

	g_stk3x1x_ptr = obj;
	/*configure to GPIO function, external interrupt*/
#ifdef CUST_EINT_ALS_TYPE
	APS_LOG("ALS/PS interrupt pin = %d\n", GPIO_STK3X1X_EINT_PIN);

	mt_set_gpio_mode(GPIO_STK3X1X_EINT_PIN, GPIO_ALS_EINT_PIN_M_EINT);
	mt_set_gpio_dir(GPIO_STK3X1X_EINT_PIN,  GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_STK3X1X_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_STK3X1X_EINT_PIN, GPIO_PULL_UP);

	mt_eint_set_hw_debounce(CUST_EINT_STK3X1X_NUM,  CUST_EINT_ALS_DEBOUNCE_CN);
	mt_eint_registration(CUST_EINT_STK3X1X_NUM,  CUST_EINT_ALS_TYPE, stk3x1x_eint_func, 0);
	mt_eint_unmask(CUST_EINT_STK3X1X_NUM);
#endif
	return 0;
}
/*----------------------------------------------------------------------------*/

static int stk3x1x_init_client(struct i2c_client *client)
{
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
	int err;
	u8 range[16];
	range[0] = 0x04;

	err = stk3x1x_write_sw_reset(client);
	if (err) {
		APS_ERR("software reset error, err=%d", err);
		return err;
	}

	if (obj->hw->polling_mode_ps == 0 || obj->hw->polling_mode_als == 0) {
		err = stk3x1x_setup_eint(client);
		if (err) {
			APS_ERR("setup eint error: %d\n", err);
			return err;
		}
	}

	err = stk3x1x_master_send(client, 0x0B, &range[0], 1);
	if (err < 0) {
		APS_ERR("i2c write range  error = %d\n", err);
		return err;
	}


	err = stk3x1x_write_als(client, 0x04);
	if (err) {
		APS_ERR("write als error: %d\n", err);
		return err;
	}

#ifdef STK_FIR
	memset(&obj->fir, 0x00, sizeof(obj->fir));
#endif
	return 0;
}

/******************************************************************************
 * Misc functions CEI
*******************************************************************************/
static int stk3x1x_i2c_read_data(struct i2c_client *client, unsigned char command, int length, unsigned char *values)
{
	uint8_t retry;
	int err;
	struct i2c_msg msgs[] =	{
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &command,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = values,
		},
	};

	for (retry = 0; retry < 5; retry++) {
		err = i2c_transfer(client->adapter, msgs, 2);
		if (err == 2)
			break;
		else
			mdelay(5);
	}

	if (retry >= 5) {
		printk(KERN_ERR "%s: i2c read fail, err=%d\n", __func__, err);
		return -EIO;
	}
	return 0;
}

static int stk3x1x_i2c_smbus_read_byte_data(struct i2c_client *client, unsigned char command)
{
	unsigned char value;
	int err;
	err = stk3x1x_i2c_read_data(client, command, 1, &value);
	if (err < 0)
		return err;
	return value;
}

inline uint32_t stk_alscode2lux(struct stk3x1x_priv *ps_data, uint32_t alscode)
{
	alscode += ((alscode<<7)+(alscode<<3)+(alscode>>1));
	alscode <<= 3;
	if (ps_data->als_transmittance > 0)
		alscode /= ps_data->als_transmittance;
	else
		return 0;

	return alscode;
}
static int32_t stk3x1x_check_pid(struct stk3x1x_priv *ps_data)
{
	int32_t err1;

	err1 = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, JSA_1214_ID);

	return err1;

}

static inline int32_t stk3x1x_get_als_reading(struct stk3x1x_priv *ps_data)
{
	int32_t word_data;
#ifdef STK_ALS_FIR
	int index;
#endif
	unsigned char value[2];
	int ret;
	ret = stk3x1x_i2c_read_data(ps_data->client, JSA_1214_ALS1, 2, &value[0]);
	if (ret < 0) {
		printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
		return ret;
	}
	word_data = ((value[1]&0x0F) << 8) | (value[0]);

#ifdef STK_ALS_FIR
	if (ps_data->fir.number < 8) {
		ps_data->fir.raw[ps_data->fir.number] = word_data;
		ps_data->fir.sum += word_data;
		ps_data->fir.number++;
		ps_data->fir.idx++;
	} else {
		index = ps_data->fir.idx % 8;
		ps_data->fir.sum -= ps_data->fir.raw[index];
		ps_data->fir.raw[index] = word_data;
		ps_data->fir.sum += word_data;
		ps_data->fir.idx++;
		word_data = ps_data->fir.sum/8;
	}
#endif
	return word_data;
}
static inline uint32_t stk3x1x_get_als_reading_AVG(struct stk3x1x_priv *ps_data, int sSampleNo)
{
	uint32_t ALSData = 0;
	uint32_t DataCount = 0;
	uint32_t sAveAlsData = 0;
	while (DataCount < sSampleNo) {
		msleep(100);
		ALSData = stk3x1x_get_als_reading(ps_data);
		printk(KERN_ERR "%s: [#23][STK]als code = %d\n", __func__, ALSData);
		sAveAlsData +=  ALSData;
		DataCount++;
	}
	sAveAlsData /= sSampleNo;
	return sAveAlsData;
}
static bool als_store_cali_transmittance_in_file(const char *filename, unsigned int value)
{
	struct file *cali_file;
	mm_segment_t fs;
	char w_buf[STK3x1x_DATA_BUF_NUM*sizeof(unsigned int)*2+1] = {0} , r_buf[STK3x1x_DATA_BUF_NUM*sizeof(unsigned int)*2+1] = {0};
	int i;
	char *dest = w_buf;

	printk(KERN_INFO "%s enter", __func__);
	cali_file = filp_open(filename, O_CREAT | O_RDWR, 0777);

	if (IS_ERR(cali_file)) {
		printk(KERN_ERR "%s open error! exit! \n", __func__);
		return false;
	} else {
		fs = get_fs();
		set_fs(get_ds());

		for (i = 0; i < STK3x1x_DATA_BUF_NUM; i++) {
			sprintf(dest,  "%02X", value & 0x000000FF);
			dest += 2;
			sprintf(dest,  "%02X", (value >> 8) & 0x000000FF);
			dest += 2;
			sprintf(dest,  "%02X", (value >> 16) & 0x000000FF);
			dest += 2;
			sprintf(dest,  "%02X", (value >> 24) & 0x000000FF);
			dest += 2;
		}

		printk("w_buf: %s \n", w_buf);
		cali_file->f_op->write(cali_file, (void *)w_buf, STK3x1x_DATA_BUF_NUM*sizeof(unsigned int)*2+1, &cali_file->f_pos);
		cali_file->f_pos = 0x00;
		cali_file->f_op->read(cali_file, (void *)r_buf, STK3x1x_DATA_BUF_NUM*sizeof(unsigned int)*2+1, &cali_file->f_pos);

		for (i = 0; i < STK3x1x_DATA_BUF_NUM*sizeof(unsigned int)*2+1; i++) {
			if (r_buf[i] != w_buf[i]) {
				filp_close(cali_file, NULL);
				printk(KERN_ERR "%s read back error! exit!\n",  __func__);
				return false;
			}
		}

		set_fs(fs);
	}

	filp_close(cali_file, NULL);
	printk("pass\n");
	return true;
}

/******************************************************************************
 * Sysfs attributes
*******************************************************************************/
static ssize_t stk3x1x_show_config(struct device_driver *ddri, char *buf)
{
	ssize_t res;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	res = scnprintf(buf, PAGE_SIZE, "(%d %d %d %d %d %d)\n",
					atomic_read(&stk3x1x_obj->i2c_retry), atomic_read(&stk3x1x_obj->als_debounce),
					atomic_read(&stk3x1x_obj->ps_mask), atomic_read(&stk3x1x_obj->ps_high_thd_val), atomic_read(&stk3x1x_obj->ps_low_thd_val), atomic_read(&stk3x1x_obj->ps_debounce));
	return res;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_config(struct device_driver *ddri, const char *buf, size_t count)
{
	int retry, als_deb, ps_deb, mask, hthres, lthres, err;
	struct i2c_client *client;
	client = stk3x1x_i2c_client;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	if (6 == sscanf(buf, "%d %d %d %d %d %d", &retry, &als_deb, &mask, &hthres, &lthres, &ps_deb)) {
		atomic_set(&stk3x1x_obj->i2c_retry, retry);
		atomic_set(&stk3x1x_obj->als_debounce, als_deb);
		atomic_set(&stk3x1x_obj->ps_mask, mask);
		atomic_set(&stk3x1x_obj->ps_high_thd_val, hthres);
		atomic_set(&stk3x1x_obj->ps_low_thd_val, lthres);
		atomic_set(&stk3x1x_obj->ps_debounce, ps_deb);

		err = stk3x1x_write_ps_high_thd(client, atomic_read(&stk3x1x_obj->ps_high_thd_val));
		if (err) {
			APS_ERR("write high thd error: %d\n", err);
			return err;
		}

		err = stk3x1x_write_ps_low_thd(client, atomic_read(&stk3x1x_obj->ps_low_thd_val));
		if (err) {
			APS_ERR("write low thd error: %d\n", err);
			return err;
		}
	} else {
		APS_ERR("invalid content: '%s', length = %zu\n", buf, count);
	}
	return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_trace(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	res = scnprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&stk3x1x_obj->trace));
	return res;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_trace(struct device_driver *ddri, const char *buf, size_t count)
{
	int trace;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	if (1 == sscanf(buf, "0x%x", &trace)) {
		atomic_set(&stk3x1x_obj->trace, trace);
	} else {
		APS_ERR("invalid content: '%s', length = %d\n", buf, (int)count);
	}
	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_ir(struct device_driver *ddri, char *buf)
{
	int32_t reading;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	reading = stk3x1x_get_ir_value(stk3x1x_obj);
	if (reading < 0)
		return scnprintf(buf, PAGE_SIZE, "ERROR: %d\n", reading);

	stk3x1x_obj->ir_code = reading;
	return scnprintf(buf, PAGE_SIZE, "0x%04X\n", stk3x1x_obj->ir_code);
}

/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_als(struct device_driver *ddri, char *buf)
{
	int res;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	res = stk3x1x_read_als(stk3x1x_obj->client, &stk3x1x_obj->als);
	if (res) {
		return scnprintf(buf, PAGE_SIZE, "ERROR: %d\n", res);
	} else {
		return scnprintf(buf, PAGE_SIZE, "0x%04X\n", stk3x1x_obj->als);
	}
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_ps(struct device_driver *ddri, char *buf)
{
	int res;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	res = stk3x1x_read_ps(stk3x1x_obj->client, &stk3x1x_obj->ps);

	if (res) {
		return scnprintf(buf, PAGE_SIZE, "ERROR: %d\n", res);
	} else {
		return scnprintf(buf, PAGE_SIZE, "0x%04X\n", stk3x1x_obj->ps);
	}
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_reg(struct device_driver *ddri, char *buf)
{
	u8 int_status;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	/*read*/
	stk3x1x_check_intr(stk3x1x_obj->client, &int_status);
	stk3x1x_read_ps(stk3x1x_obj->client, &stk3x1x_obj->ps);
	stk3x1x_read_als(stk3x1x_obj->client, &stk3x1x_obj->als);
	/*write*/
	stk3x1x_write_als(stk3x1x_obj->client, atomic_read(&stk3x1x_obj->alsctrl_val));
	stk3x1x_write_ps(stk3x1x_obj->client, atomic_read(&stk3x1x_obj->psctrl_val));
	stk3x1x_write_ps_high_thd(stk3x1x_obj->client, atomic_read(&stk3x1x_obj->ps_high_thd_val));
	stk3x1x_write_ps_low_thd(stk3x1x_obj->client, atomic_read(&stk3x1x_obj->ps_low_thd_val));
	return 0;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_send(struct device_driver *ddri, char *buf)
{
	return 0;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_send(struct device_driver *ddri, const char *buf, size_t count)
{
	int addr, cmd;
	u8 dat;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	} else if (2 != sscanf(buf, "%x %x", &addr, &cmd)) {
		APS_ERR("invalid format: '%s'\n", buf);
		return 0;
	}

	dat = (u8)cmd;
	APS_LOG("send(%02X, %02X) = %d\n", addr, cmd,
			stk3x1x_master_send(stk3x1x_obj->client, (u16)addr, &dat, sizeof(dat)));

	return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_recv(struct device_driver *ddri, char *buf)
{
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&stk3x1x_obj->recv_reg));
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_recv(struct device_driver *ddri, const char *buf, size_t count)
{
	int addr;
	u8 dat;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	} else if (1 != sscanf(buf, "%x", &addr)) {
		APS_ERR("invalid format: '%s'\n", buf);
		return 0;
	}

	APS_LOG("recv(%02X) = %d, 0x%02X\n", addr,
			stk3x1x_master_recv(stk3x1x_obj->client, (u16)addr, (char *)&dat, sizeof(dat)), dat);
	atomic_set(&stk3x1x_obj->recv_reg, dat);
	return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_allreg(struct device_driver *ddri, char *buf)
{
	int ret = 0;
	u8 rbuf[27];
	int cnt;

	memset(rbuf, 0, sizeof(rbuf));
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 0, &rbuf[0], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 7, &rbuf[7], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 14, &rbuf[14], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 21, &rbuf[21], 4);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, STK_PDT_ID_REG, &rbuf[25], 2);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}

	for (cnt = 0; cnt < 25; cnt++) {
		APS_LOG("reg[0x%x]=0x%x\n", cnt, rbuf[cnt]);
	}
	APS_LOG("reg[0x3E]=0x%x\n", rbuf[cnt]);
	APS_LOG("reg[0x3F]=0x%x\n", rbuf[cnt++]);
	return scnprintf(buf, PAGE_SIZE, "[0]%2X [1]%2X [2]%2X [3]%2X [4]%2X [5]%2X [6/7 HTHD]%2X,%2X [8/9 LTHD]%2X, %2X [A]%2X [B]%2X [C]%2X [D]%2X [E/F Aoff]%2X,%2X,[10]%2X [11/12 PS]%2X,%2X [13]%2X [14]%2X [15/16 Foff]%2X,%2X [17]%2X [18]%2X [3E]%2X [3F]%2X\n",
					 rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7], rbuf[8],
					 rbuf[9], rbuf[10], rbuf[11], rbuf[12], rbuf[13], rbuf[14], rbuf[15], rbuf[16], rbuf[17],
					 rbuf[18], rbuf[19], rbuf[20], rbuf[21], rbuf[22], rbuf[23], rbuf[24], rbuf[25], rbuf[26]);
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_status(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	u8 rbuf[25];
	int ret = 0;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	if (stk3x1x_obj->hw) {
		len += scnprintf(buf+len, PAGE_SIZE-len, "CUST: %d, (%d %d) (%02X) (%02X %02X %02X) (%02X %02X %02X %02X)\n",
						 stk3x1x_obj->hw->i2c_num, stk3x1x_obj->hw->power_id, stk3x1x_obj->hw->power_vol, stk3x1x_obj->addr.flag,
						 stk3x1x_obj->addr.alsctrl, stk3x1x_obj->addr.data1_als, stk3x1x_obj->addr.data2_als, stk3x1x_obj->addr.psctrl,
						 stk3x1x_obj->addr.data1_ps, stk3x1x_obj->addr.data2_ps, stk3x1x_obj->addr.thdh1_ps);
	} else {
		len += scnprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");
	}

	len += scnprintf(buf+len, PAGE_SIZE-len, "REGS: %02X %02X %02X %02X %02X %02X %02X %02X %02lX %02lX\n",
					 atomic_read(&stk3x1x_obj->state_val), atomic_read(&stk3x1x_obj->psctrl_val), atomic_read(&stk3x1x_obj->alsctrl_val),
					 stk3x1x_obj->ledctrl_val, stk3x1x_obj->int_val, stk3x1x_obj->wait_val,
					 atomic_read(&stk3x1x_obj->ps_high_thd_val), atomic_read(&stk3x1x_obj->ps_low_thd_val), stk3x1x_obj->enable, stk3x1x_obj->pending_intr);

	len += scnprintf(buf+len, PAGE_SIZE-len, "MISC: %d %d\n", atomic_read(&stk3x1x_obj->als_suspend), atomic_read(&stk3x1x_obj->ps_suspend));
	len += scnprintf(buf+len, PAGE_SIZE-len, "VER.: %s\n", DRIVER_VERSION);

	memset(rbuf, 0, sizeof(rbuf));
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 0, &rbuf[0], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 7, &rbuf[7], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 14, &rbuf[14], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	len += scnprintf(buf+len, PAGE_SIZE-len, "[PS=%2X] [ALS=%2X] [WAIT=%4Xms] [EN_ASO=%2X] [EN_AK=%2X] [NEAR/FAR=%2X] [FLAG_OUI=%2X] [FLAG_PSINT=%2X] [FLAG_ALSINT=%2X]\n",
					 rbuf[0]&0x01, (rbuf[0]&0x02)>>1, ((rbuf[0]&0x04)>>2)*rbuf[5]*6, (rbuf[0]&0x20)>>5,
					 (rbuf[0]&0x40)>>6, rbuf[16]&0x01, (rbuf[16]&0x04)>>2, (rbuf[16]&0x10)>>4, (rbuf[16]&0x20)>>5);

	return len;
}
/*----------------------------------------------------------------------------*/
#define IS_SPACE(CH) (((CH) == ' ') || ((CH) == '\n'))
/*----------------------------------------------------------------------------*/
static int read_int_from_buf(struct stk3x1x_priv *obj, const char *buf, size_t count,
							 u32 data[], int len)
{
	int idx = 0;
	char *cur = (char *)buf, *end = (char *)(buf+count);

	while (idx < len) {
		while ((cur < end) && IS_SPACE(*cur)) {
			cur++;
		}

		if (1 != sscanf(cur, "%d", &data[idx])) {
			break;
		}

		idx++;
		while ((cur < end) && !IS_SPACE(*cur)) {
			cur++;
		}
	}
	return idx;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_alslv(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	int idx;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	for (idx = 0; idx < stk3x1x_obj->als_level_num; idx++) {
		len += scnprintf(buf+len, PAGE_SIZE-len, "%d ", stk3x1x_obj->hw->als_level[idx]);
	}
	len += scnprintf(buf+len, PAGE_SIZE-len, "\n");
	return len;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_alslv(struct device_driver *ddri, const char *buf, size_t count)
{
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	} else if (!strcmp(buf, "def")) {
		memcpy(stk3x1x_obj->als_level, stk3x1x_obj->hw->als_level, sizeof(stk3x1x_obj->als_level));
	} else if (stk3x1x_obj->als_level_num != read_int_from_buf(stk3x1x_obj, buf, count,
															   stk3x1x_obj->hw->als_level, stk3x1x_obj->als_level_num)) {
		APS_ERR("invalid format: '%s'\n", buf);
	}
	return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_show_alsval(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	int idx;
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}

	for (idx = 0; idx < stk3x1x_obj->als_value_num; idx++) {
		len += scnprintf(buf+len, PAGE_SIZE-len, "%d ", stk3x1x_obj->hw->als_value[idx]);
	}
	len += scnprintf(buf+len, PAGE_SIZE-len, "\n");
	return len;
}
/*----------------------------------------------------------------------------*/
static ssize_t stk3x1x_store_alsval(struct device_driver *ddri, const char *buf, size_t count)
{
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	} else if (!strcmp(buf, "def")) {
		memcpy(stk3x1x_obj->als_value, stk3x1x_obj->hw->als_value, sizeof(stk3x1x_obj->als_value));
	} else if (stk3x1x_obj->als_value_num != read_int_from_buf(stk3x1x_obj, buf, count,
															   stk3x1x_obj->hw->als_value, stk3x1x_obj->als_value_num)) {
		APS_ERR("invalid format: '%s'\n", buf);
	}
	return count;
}

/******************************************************************************
 * Sysfs attributes CEI
*******************************************************************************/
static ssize_t stk_als_enable_show(struct device_driver *ddri, char *buf)
{
	int32_t enable, ret;

	mutex_lock(&stk3x1x_obj->io_lock);
	enable = (stk3x1x_obj->als_enabled)?1:0;
	mutex_unlock(&stk3x1x_obj->io_lock);
	ret = stk3x1x_i2c_smbus_read_byte_data(stk3x1x_obj->client, STK_STATE_REG);
	ret = (ret & STK_STATE_EN_ALS_MASK)?1:0;

	if (enable != ret)
		printk(KERN_ERR "%s: driver and sensor mismatch! driver_enable=0x%x, sensor_enable=%x\n", __func__, enable, ret);

	return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t stk_als_enable_store(struct device_driver *ddri, const char *buf, size_t size)
{
	uint8_t en;
	if (sysfs_streq(buf, "1"))
		en = 1;
	else if (sysfs_streq(buf, "0"))
		en = 0;
	else {
		printk(KERN_ERR "%s, invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}
	printk(KERN_INFO "%s: Enable ALS : %d\n", __func__, en);
	mutex_lock(&stk3x1x_obj->io_lock);
	stk3x1x_enable_als(stk3x1x_obj->client, en);
	mutex_unlock(&stk3x1x_obj->io_lock);
	return size;
}

static ssize_t stk_als_lux_show(struct device_driver *ddri, char *buf)
{
	int32_t als_reading;
	uint32_t als_lux;
	als_reading = stk3x1x_get_als_reading(stk3x1x_obj);
	als_lux = stk_alscode2lux(stk3x1x_obj, als_reading);
	return scnprintf(buf, PAGE_SIZE, "%d lux\n", als_lux);
}

static ssize_t stk_als_code_show(struct device_driver *ddri, char *buf)
{
	int res;

	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	res = stk3x1x_read_als(stk3x1x_obj->client, &stk3x1x_obj->als);
	if (res) {
		return scnprintf(buf, PAGE_SIZE, "ERROR: %d\n", res);
	} else {
		return scnprintf(buf, PAGE_SIZE, "%d\n", stk3x1x_obj->als);
	}
}
static ssize_t stk_als_transmittance_show(struct device_driver *ddri, char *buf)
{
	int32_t transmittance;
	transmittance = stk3x1x_obj->als_transmittance;
	return scnprintf(buf, PAGE_SIZE, "%d\n", transmittance);
}

static ssize_t stk_als_transmittance_store(struct device_driver *ddri, const char *buf, size_t size)
{
	unsigned long value = 0;
	int ret;
	ret = kstrtoul(buf, 10, &value);
	if (ret < 0) {
		printk(KERN_ERR "%s:strict_strtoul failed, ret=0x%x\n", __func__, ret);
		return ret;
	}
	stk3x1x_obj->als_transmittance = value;
	return size;
}

static ssize_t als_CCI_test_Light_show(struct device_driver *ddri, char *buf)
{
	int32_t als_reading;

	printk(KERN_ERR "%s:[#23][STK]Start testing light...\n", __func__);
	msleep(150);
	als_reading = stk3x1x_get_als_reading_AVG(stk3x1x_obj, 5);
	mutex_lock(&stk3x1x_obj->io_lock);
	printk(KERN_ERR "%s:[#23][STK]als_reading = %d\n", __func__, als_reading);
	cci_als_value = stk_alscode2lux(stk3x1x_obj, als_reading);
	mutex_unlock(&stk3x1x_obj->io_lock);
	printk(KERN_ERR "%s:[#23][STK]Start testing light done!!! cci_als_value = %d lux, cci_als_test_adc = %d \n", __func__, cci_als_value, als_reading);
	return scnprintf(buf, PAGE_SIZE, "cci_als_value = %5d lux, cci_als_test_adc = %5d \n", cci_als_value, als_reading);
}

static ssize_t als_CCI_cali_Light_show(struct device_driver *ddri, char *buf)
{
	int32_t als_reading;
	unsigned int cci_als_value_cali_adc;
	bool result = false;
	printk(KERN_ERR "%s:[#23][STK]Start Cali light...\n", __func__);
	msleep(150);
	als_reading = stk3x1x_get_als_reading_AVG(stk3x1x_obj, 5);
	cci_als_value_cali_adc = als_reading;
	mutex_lock(&stk3x1x_obj->io_lock);
	cci_als_value_cali = stk_alscode2lux(stk3x1x_obj, als_reading);

	if (((cci_als_value_cali * stk3x1x_obj->als_transmittance)/400) > 0 && (cci_als_value_cali_adc <= 65535)) {
		cci_transmittance_cali = (cci_als_value_cali * stk3x1x_obj->als_transmittance)/400;
		stk3x1x_obj->als_transmittance = cci_transmittance_cali;
		result = als_store_cali_transmittance_in_file(STK3X1X_CAL_FILE, cci_transmittance_cali);
		printk(KERN_INFO "%s: result:=%d\n", __func__, result);
		cci_als_value_cali = stk_alscode2lux(stk3x1x_obj, als_reading);
		printk(KERN_ERR "%s:[#23][STK]cali light done!!! cci_als_value_cali = %d lux, cci_transmittance_cali = %d, cci_als_value_cali_adc = %d code\n", __func__, cci_als_value_cali, cci_transmittance_cali, cci_als_value_cali_adc);

	} else {
		printk(KERN_ERR "%s:[#23][STK]cali light fail!!! cci_als_value_cali = %d lux, cci_transmittance_cali = %d, cci_als_value_cali_adc = %d code\n", __func__, cci_als_value_cali, cci_transmittance_cali, cci_als_value_cali_adc);
		result = false;
	}
	mutex_unlock(&stk3x1x_obj->io_lock);
	return scnprintf(buf, PAGE_SIZE, "%s: cci_als_value_cali = %d lux, cci_transmittance_cali = %d, cci_als_value_cali_adc = %d code\n", result ? "PASSED" : "FAIL", cci_als_value_cali, cci_transmittance_cali, cci_als_value_cali_adc);
}

static ssize_t stk_all_reg_show(struct device_driver *ddri, char *buf)
{
	int ret = 0;
	u8 rbuf[27];
	int cnt;

	memset(rbuf, 0, sizeof(rbuf));
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return 0;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 0, &rbuf[0], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 7, &rbuf[7], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 14, &rbuf[14], 7);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, 21, &rbuf[21], 4);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(stk3x1x_obj->client, STK_PDT_ID_REG, &rbuf[25], 2);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}

	for (cnt = 0; cnt < 25; cnt++) {
		APS_LOG("reg[0x%x]=0x%x\n", cnt, rbuf[cnt]);
	}
	APS_LOG("reg[0x3E]=0x%x\n", rbuf[cnt]);
	APS_LOG("reg[0x3F]=0x%x\n", rbuf[cnt++]);
	return scnprintf(buf, PAGE_SIZE, "[0]%2X [1]%2X [2]%2X [3]%2X [4]%2X [5]%2X [6/7 HTHD]%2X,%2X [8/9 LTHD]%2X, %2X [A]%2X [B]%2X [C]%2X [D]%2X [E/F Aoff]%2X,%2X,[10]%2X [11/12 PS]%2X,%2X [13]%2X [14]%2X [15/16 Foff]%2X,%2X [17]%2X [18]%2X [3E]%2X [3F]%2X\n",
					 rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7], rbuf[8],
					 rbuf[9], rbuf[10], rbuf[11], rbuf[12], rbuf[13], rbuf[14], rbuf[15], rbuf[16], rbuf[17],
					 rbuf[18], rbuf[19], rbuf[20], rbuf[21], rbuf[22], rbuf[23], rbuf[24], rbuf[25], rbuf[26]);
}

static ssize_t pin_sensor_show(struct device_driver *ddri, char *buf)
{
	int32_t ret = stk3x1x_check_pid(stk3x1x_obj);
	if (ret >= 0) {
		printk("STK check PID = %d success\n", ret);
		return scnprintf(buf, PAGE_SIZE, "[stk] I2C Work, ID = %x\n", ret);
	} else {
		printk("STK check PID = %d fail\n", ret);
		return scnprintf(buf, PAGE_SIZE, "[stk] I2C mayFail!\n Check ID = %x number\n", ret);
	}
}


/*----------------------------------------------------------------------------*/
static DRIVER_ATTR(als,     S_IWUSR | S_IRUGO, stk3x1x_show_als,   NULL);
static DRIVER_ATTR(ps,      S_IWUSR | S_IRUGO, stk3x1x_show_ps,    NULL);
static DRIVER_ATTR(ir,      S_IWUSR | S_IRUGO, stk3x1x_show_ir,    NULL);
static DRIVER_ATTR(config,  S_IWUSR | S_IRUGO, stk3x1x_show_config, stk3x1x_store_config);
static DRIVER_ATTR(alslv,   S_IWUSR | S_IRUGO, stk3x1x_show_alslv, stk3x1x_store_alslv);
static DRIVER_ATTR(alsval,  S_IWUSR | S_IRUGO, stk3x1x_show_alsval, stk3x1x_store_alsval);
static DRIVER_ATTR(trace,   S_IWUSR | S_IRUGO, stk3x1x_show_trace, stk3x1x_store_trace);
static DRIVER_ATTR(status,  S_IWUSR | S_IRUGO, stk3x1x_show_status, NULL);
static DRIVER_ATTR(send,    S_IWUSR | S_IRUGO, stk3x1x_show_send, stk3x1x_store_send);
static DRIVER_ATTR(recv,    S_IWUSR | S_IRUGO, stk3x1x_show_recv,  stk3x1x_store_recv);
static DRIVER_ATTR(reg,     S_IWUSR | S_IRUGO, stk3x1x_show_reg,   NULL);
static DRIVER_ATTR(allreg_ori,  S_IWUSR | S_IRUGO, stk3x1x_show_allreg,   NULL);
/*=============================================================*/
static DRIVER_ATTR(enable, S_IWUSR | S_IRUGO, stk_als_enable_show, stk_als_enable_store);
static DRIVER_ATTR(lux, S_IWUSR | S_IRUGO, stk_als_lux_show, NULL);
static DRIVER_ATTR(code, S_IWUSR | S_IRUGO, stk_als_code_show, NULL);
static DRIVER_ATTR(transmittance, S_IWUSR | S_IRUGO, stk_als_transmittance_show, stk_als_transmittance_store);
static DRIVER_ATTR(test_Light, S_IWUSR | S_IRUGO, als_CCI_test_Light_show, NULL);
static DRIVER_ATTR(cali_Light, S_IWUSR | S_IRUGO, als_CCI_cali_Light_show, NULL);
static DRIVER_ATTR(allreg, S_IWUSR | S_IRUGO, stk_all_reg_show, NULL);
static DRIVER_ATTR(pin_sensor, S_IWUSR | S_IRUGO, pin_sensor_show, NULL);
/*----------------------------------------------------------------------------*/
static struct driver_attribute *stk3x1x_attr_list[] = {
	&driver_attr_als,
	&driver_attr_ps,
	&driver_attr_ir,
	&driver_attr_trace,		   /*trace log*/
	&driver_attr_config,
	&driver_attr_alslv,
	&driver_attr_alsval,
	&driver_attr_status,
	&driver_attr_send,
	&driver_attr_recv,
	&driver_attr_allreg_ori,
	&driver_attr_reg,
	&driver_attr_enable,   /*cei start*/
	&driver_attr_lux,
	&driver_attr_code,
	&driver_attr_transmittance,
	&driver_attr_test_Light,
	&driver_attr_cali_Light,
	&driver_attr_allreg,
	&driver_attr_pin_sensor,  /*cei end*/
};

/*----------------------------------------------------------------------------*/
static int stk3x1x_create_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(stk3x1x_attr_list)/sizeof(stk3x1x_attr_list[0]));
	if (driver == NULL) {
		return -EINVAL;
	}

	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, stk3x1x_attr_list[idx]);
		if (err) {
			APS_ERR("driver_create_file (%s) = %d\n", stk3x1x_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}
/*----------------------------------------------------------------------------*/
static int stk3x1x_delete_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(stk3x1x_attr_list)/sizeof(stk3x1x_attr_list[0]));

	if (!driver)
		return -EINVAL;

	for (idx = 0; idx < num; idx++) {
		driver_remove_file(driver, stk3x1x_attr_list[idx]);
	}

	return err;
}
/******************************************************************************
 * Function Configuration
******************************************************************************/
static int stk3x1x_get_als_value(struct stk3x1x_priv *obj, u16 als)
{
	int idx;
	int invalid = 0;
	for (idx = 0; idx < obj->als_level_num; idx++) {
		if (als < obj->hw->als_level[idx]) {
			break;
		}
	}

	if (idx >= obj->als_value_num) {
		APS_ERR("exceed range\n");
		idx = obj->als_value_num - 1;
	}

	if (1 == atomic_read(&obj->als_deb_on)) {
		unsigned long endt = atomic_read(&obj->als_deb_end);
		if (time_after(jiffies, endt)) {
			atomic_set(&obj->als_deb_on, 0);
		}

		if (1 == atomic_read(&obj->als_deb_on)) {
			invalid = 1;
		}
	}

	if (!invalid) {
#if defined(CONFIG_MTK_AAL_SUPPORT)
		int level_high = obj->hw->als_level[idx];
		int level_low = (idx > 0) ? obj->hw->als_level[idx-1] : 0;
		int level_diff = level_high - level_low;
		int value_high = obj->hw->als_value[idx];
		int value_low = (idx > 0) ? obj->hw->als_value[idx-1] : 0;
		int value_diff = value_high - value_low;
		int value = 0;

		if ((level_low >= level_high) || (value_low >= value_high))
			value = value_low;
		else
			value = (level_diff * value_low + (als - level_low) * value_diff + ((level_diff + 1) >> 1)) / level_diff;
		APS_DBG("ALS: %d [%d, %d] => %d [%d, %d] \n", als, level_low, level_high, value, value_low, value_high);
		return value;
#endif

		if (atomic_read(&obj->trace) & STK_TRC_CVT_ALS) {
			APS_DBG("ALS: %05d => %05d\n", als, obj->hw->als_value[idx]);
		}

		return obj->hw->als_value[idx];
	} else {
		if (atomic_read(&obj->trace) & STK_TRC_CVT_ALS) {
			APS_DBG("ALS: %05d => %05d (-1)\n", als, obj->hw->als_value[idx]);
		}
		return -1;
	}
}

/*----------------------------------------------------------------------------*/
static int stk3x1x_get_ps_value_only(struct stk3x1x_priv *obj, u16 ps)
{
	int mask = atomic_read(&obj->ps_mask);
	int invalid = 0, val;
	int err;
	u8 flag;

	err = stk3x1x_read_flag(obj->client, &flag);
	if (err)
		return err;
	val = (flag & STK_FLG_NF_MASK) ? 1 : 0;

	if (atomic_read(&obj->ps_suspend)) {
		invalid = 1;
	} else if (1 == atomic_read(&obj->ps_deb_on)) {
		unsigned long endt = atomic_read(&obj->ps_deb_end);
		if (time_after(jiffies, endt)) {
			atomic_set(&obj->ps_deb_on, 0);
		}

		if (1 == atomic_read(&obj->ps_deb_on)) {
			invalid = 1;
		}
	}

	if (!invalid) {
		if (unlikely(atomic_read(&obj->trace) & STK_TRC_CVT_PS)) {
			if (mask) {
				APS_DBG("PS:  %05d => %05d [M] \n", ps, val);
			} else {
				APS_DBG("PS:  %05d => %05d\n", ps, val);
			}
		}
		return val;

	} else {
		APS_ERR(" ps value is invalid, PS:	%05d => %05d\n", ps, val);
		if (unlikely(atomic_read(&obj->trace) & STK_TRC_CVT_PS)) {
			APS_DBG("PS:  %05d => %05d (-1)\n", ps, val);
		}
		return -1;
	}
}

/*----------------------------------------------------------------------------*/
static int stk3x1x_get_ps_value(struct stk3x1x_priv *obj, u16 ps)
{
	int mask = atomic_read(&obj->ps_mask);
	int invalid = 0, val;
	int err;
	u8 flag;

	err = stk3x1x_read_flag(obj->client, &flag);
	if (err)
		return err;

	val = (flag & STK_FLG_NF_MASK) ? 1 : 0;
	err = stk3x1x_clear_intr(obj->client, flag, STK_FLG_OUI_MASK);
	if (err) {
		APS_ERR("fail: %d\n", err);
		return err;
	}

	if (atomic_read(&obj->ps_suspend)) {
		invalid = 1;
	} else if (1 == atomic_read(&obj->ps_deb_on)) {
		unsigned long endt = atomic_read(&obj->ps_deb_end);
		if (time_after(jiffies, endt)) {
			atomic_set(&obj->ps_deb_on, 0);
		}

		if (1 == atomic_read(&obj->ps_deb_on)) {
			invalid = 1;
		}
	}


	if (!invalid) {
		if (unlikely(atomic_read(&obj->trace) & STK_TRC_CVT_PS)) {
			if (mask) {
				APS_DBG("PS:  %05d => %05d [M] \n", ps, val);
			} else {
				APS_DBG("PS:  %05d => %05d\n", ps, val);
			}
		}
		return val;

	} else {
		APS_ERR(" ps value is invalid, PS:	%05d => %05d\n", ps, val);
		if (unlikely(atomic_read(&obj->trace) & STK_TRC_CVT_PS)) {
			APS_DBG("PS:  %05d => %05d (-1)\n", ps, val);
		}
		return -1;
	}
}

/*----------------------------------------------------------------------------*/
static int32_t stk3x1x_set_irs_it_slp(struct stk3x1x_priv *obj, uint16_t *slp_time)
{
	uint8_t irs_alsctrl;
	int32_t ret;

	irs_alsctrl = (atomic_read(&obj->alsctrl_val) & 0x0F) - 2;
	switch (irs_alsctrl) {
	case 6:
		*slp_time = 12;
		break;
	case 7:
		*slp_time = 24;
		break;
	case 8:
		*slp_time = 48;
		break;
	case 9:
		*slp_time = 96;
		break;
	default:
		printk(KERN_ERR "%s: unknown ALS IT=0x%x\n", __func__, irs_alsctrl);
		ret = -EINVAL;
		return ret;
	}
	irs_alsctrl |= (atomic_read(&obj->alsctrl_val) & 0xF0);
	ret = i2c_smbus_write_byte_data(obj->client, STK_ALSCTRL_REG, irs_alsctrl);
	if (ret < 0) {
		printk(KERN_ERR "%s: write i2c error\n", __func__);
		return ret;
	}
	return 0;
}

static int32_t stk3x1x_get_ir_value(struct stk3x1x_priv *obj)
{
	int32_t word_data, ret;
	uint8_t w_reg, retry = 0;
	uint16_t irs_slp_time = 100;
	bool re_enable_ps = false;
	u8 flag;
	u8 buf[2];

	re_enable_ps = (atomic_read(&obj->state_val) & STK_STATE_EN_PS_MASK) ? true : false;
	if (re_enable_ps) {
		stk3x1x_enable_ps(obj->client, 0);
	}

	ret = stk3x1x_set_irs_it_slp(obj, &irs_slp_time);
	if (ret < 0)
		goto irs_err_i2c_rw;

	w_reg = atomic_read(&obj->state_val) | STK_STATE_EN_IRS_MASK;
	ret = i2c_smbus_write_byte_data(obj->client, STK_STATE_REG, w_reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: write i2c error\n", __func__);
		goto irs_err_i2c_rw;
	}
	msleep(irs_slp_time);

	do {
		msleep(3);
		ret = stk3x1x_read_flag(obj->client, &flag);
		if (ret < 0) {
			APS_ERR("WARNING: read flag reg error: %d\n", ret);
			goto irs_err_i2c_rw;
		}
		retry++;
	} while (retry < 10 && ((flag&STK_FLG_IR_RDY_MASK) == 0));

	if (retry == 10) {
		printk(KERN_ERR "%s: ir data is not ready for 300ms\n", __func__);
		ret = -EINVAL;
		goto irs_err_i2c_rw;
	}

	ret = stk3x1x_clear_intr(obj->client, flag, STK_FLG_IR_RDY_MASK);
	if (ret < 0) {
		printk(KERN_ERR "%s: write i2c error\n", __func__);
		goto irs_err_i2c_rw;
	}

	ret = stk3x1x_master_recv(obj->client, STK_DATA1_IR_REG, buf, 2);
	if (ret < 0) {
		printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
		goto irs_err_i2c_rw;
	}
	word_data =  (buf[0] << 8) | buf[1];

	ret = i2c_smbus_write_byte_data(obj->client, STK_ALSCTRL_REG, atomic_read(&obj->alsctrl_val));
	if (ret < 0) {
		printk(KERN_ERR "%s: write i2c error\n", __func__);
		goto irs_err_i2c_rw;
	}
	if (re_enable_ps)
		stk3x1x_enable_ps(obj->client, 1);
	return word_data;

	irs_err_i2c_rw:
	if (re_enable_ps)
		stk3x1x_enable_ps(obj->client, 1);
	return ret;
}

/******************************************************************************
 * Function Configuration
******************************************************************************/
static int stk3x1x_open(struct inode *inode, struct file *file)
{
	file->private_data = stk3x1x_i2c_client;

	if (!file->private_data) {
		APS_ERR("null pointer!!\n");
		return -EINVAL;
	}

	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int stk3x1x_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
static long stk3x1x_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
static int stk3x1x_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	struct stk3x1x_priv *obj = i2c_get_clientdata(client);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	long err = 0;
#else
	int err = 0;
#endif
	void __user *ptr = (void __user *) arg;
	int dat;
	uint32_t enable;
	int ps_result;
	int ps_cali;
	int threshold[2];

	switch (cmd) {
	case ALSPS_SET_PS_MODE:
		if (copy_from_user(&enable, ptr, sizeof(enable))) {
			err = -EFAULT;
			goto err_out;
		}
		if (enable) {
			err = stk3x1x_enable_ps(obj->client, 1);
			if (err) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
				APS_ERR("enable ps fail: %ld\n", err);
#else
				APS_ERR("enable ps fail: %d\n", err);
#endif
				goto err_out;
			}

			set_bit(STK_BIT_PS, &obj->enable);
		} else {
		err = stk3x1x_enable_ps(obj->client, 0);
			if (err) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
				APS_ERR("disable ps fail: %ld\n", err);
#else
				APS_ERR("disable ps fail: %d\n", err);
#endif

				goto err_out;
			}

			clear_bit(STK_BIT_PS, &obj->enable);
		}
		break;

	case ALSPS_GET_PS_MODE:
		enable = test_bit(STK_BIT_PS, &obj->enable) ? (1) : (0);
		if (copy_to_user(ptr, &enable, sizeof(enable))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_GET_PS_DATA:
		err = stk3x1x_read_ps(obj->client, &obj->ps);
		if (err) {
			goto err_out;
		}

		dat = stk3x1x_get_ps_value(obj, obj->ps);
		if (dat < 0) {
			err = dat;
			goto    err_out;
		}
#ifdef STK_PS_POLLING_LOG
		APS_LOG("%s:ps raw 0x%x -> value 0x%x \n", __FUNCTION__, obj->ps, dat);
#endif
		if (copy_to_user(ptr, &dat, sizeof(dat))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_GET_PS_RAW_DATA:
		err = stk3x1x_read_ps(obj->client, &obj->ps);
		if (err) {
			goto err_out;
		}

		dat = obj->ps;
		if (copy_to_user(ptr, &dat, sizeof(dat))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_SET_ALS_MODE:
		if (copy_from_user(&enable, ptr, sizeof(enable))) {
			err = -EFAULT;
			goto err_out;
		}
		if (enable) {
			err = stk3x1x_enable_als(obj->client, 1);
			if (err) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
				APS_ERR("enable als fail: %ld\n", err);
#else
				APS_ERR("enable als fail: %d\n", err);
#endif

				goto err_out;
			}
			set_bit(STK_BIT_ALS, &obj->enable);
		} else {
		err = stk3x1x_enable_als(obj->client, 0);
			if (err) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
				APS_ERR("disable als fail: %ld\n", err);
#else
				APS_ERR("disable als fail: %d\n", err);
#endif

				goto err_out;
			}
			clear_bit(STK_BIT_ALS, &obj->enable);
		}
		break;

	case ALSPS_GET_ALS_MODE:
		enable = test_bit(STK_BIT_ALS, &obj->enable) ? (1) : (0);
		if (copy_to_user(ptr, &enable, sizeof(enable))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_GET_ALS_DATA:
		err = stk3x1x_read_als(obj->client, &obj->als);
		if (err) {
			goto err_out;
		}

		dat = stk3x1x_get_als_value(obj, obj->als);
		if (copy_to_user(ptr, &dat, sizeof(dat))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_GET_ALS_RAW_DATA:
		err = stk3x1x_read_als(obj->client, &obj->als);
		if (err) {
			goto err_out;
		}

		dat = obj->als;
		if (copy_to_user(ptr, &dat, sizeof(dat))) {
			err = -EFAULT;
			goto err_out;
		}
		break;
		/*----------------------------------for factory mode test---------------------------------------*/
	case ALSPS_GET_PS_TEST_RESULT:
		err = stk3x1x_read_ps(obj->client, &obj->ps);
		if (err) {
			goto err_out;
		}
		if (obj->ps > atomic_read(&obj->ps_high_thd_val)) {
			ps_result = 0;
		} else
			ps_result = 1;

		if (copy_to_user(ptr, &ps_result, sizeof(ps_result))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_IOCTL_CLR_CALI:
		if (copy_from_user(&dat, ptr, sizeof(dat))) {
			err = -EFAULT;
			goto err_out;
		}
		if (dat == 0)
			obj->ps_cali = 0;
		break;

	case ALSPS_IOCTL_GET_CALI:
		ps_cali = obj->ps_cali ;
		if (copy_to_user(ptr, &ps_cali, sizeof(ps_cali))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_IOCTL_SET_CALI:
		if (copy_from_user(&ps_cali, ptr, sizeof(ps_cali))) {
			err = -EFAULT;
			goto err_out;
		}

		obj->ps_cali = ps_cali;
		break;

	case ALSPS_SET_PS_THRESHOLD:
		if (copy_from_user(threshold, ptr, sizeof(threshold))) {
			err = -EFAULT;
			goto err_out;
		}
		APS_ERR("%s set threshold high: 0x%x, low: 0x%x\n", __func__, threshold[0], threshold[1]);
		atomic_set(&obj->ps_high_thd_val,  (threshold[0]+obj->ps_cali));
		atomic_set(&obj->ps_low_thd_val,  (threshold[1]+obj->ps_cali));

		err = stk3x1x_write_ps_high_thd(obj->client, atomic_read(&obj->ps_high_thd_val));
		if (err) {
			APS_ERR("write high thd error: %ld\n", err);
			goto err_out;
		}

		err = stk3x1x_write_ps_low_thd(obj->client, atomic_read(&obj->ps_low_thd_val));

		if (err) {
			APS_ERR("write low thd error: %ld\n", err);
			goto err_out;
		}

		break;

	case ALSPS_GET_PS_THRESHOLD_HIGH:
		threshold[0] = atomic_read(&obj->ps_high_thd_val) - obj->ps_cali;
		APS_ERR("%s get threshold high: 0x%x\n", __func__, threshold[0]);
		if (copy_to_user(ptr, &threshold[0], sizeof(threshold[0]))) {
			err = -EFAULT;
			goto err_out;
		}
		break;

	case ALSPS_GET_PS_THRESHOLD_LOW:
		threshold[0] = atomic_read(&obj->ps_low_thd_val) - obj->ps_cali;
		APS_ERR("%s get threshold low: 0x%x\n", __func__, threshold[0]);
		if (copy_to_user(ptr, &threshold[0], sizeof(threshold[0]))) {
			err = -EFAULT;
			goto err_out;
		}
		break;
		/*------------------------------------------------------------------------------------------*/

	default:
		APS_ERR("%s not supported = 0x%04x", __FUNCTION__, cmd);
		err = -ENOIOCTLCMD;
		break;
	}

	err_out:
	return err;
}

#ifdef CONFIG_COMPAT
static long jsa1214_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err = 0;

	void __user *arg32 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_ALSPS_SET_PS_MODE:
		err = file->f_op->unlocked_ioctl(file, ALSPS_SET_PS_MODE, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_MODE:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_MODE, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_DATA:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_DATA, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_RAW_DATA:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_RAW_DATA, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_SET_ALS_MODE:
		err = file->f_op->unlocked_ioctl(file, ALSPS_SET_ALS_MODE, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_ALS_MODE:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_ALS_MODE, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_ALS_DATA:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_ALS_DATA, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_ALS_RAW_DATA:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_ALS_RAW_DATA, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_TEST_RESULT:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_TEST_RESULT, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_IOCTL_CLR_CALI:
		err = file->f_op->unlocked_ioctl(file, ALSPS_IOCTL_CLR_CALI, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_IOCTL_GET_CALI:
		err = file->f_op->unlocked_ioctl(file, ALSPS_IOCTL_GET_CALI, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_IOCTL_SET_CALI:
		err = file->f_op->unlocked_ioctl(file, ALSPS_IOCTL_SET_CALI, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_SET_PS_THRESHOLD:
		err = file->f_op->unlocked_ioctl(file, ALSPS_SET_PS_THRESHOLD, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_THRESHOLD_HIGH:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_THRESHOLD_HIGH, (unsigned long)arg32);
		break;
	case COMPAT_ALSPS_GET_PS_THRESHOLD_LOW:
		err = file->f_op->unlocked_ioctl(file, ALSPS_GET_PS_THRESHOLD_LOW, (unsigned long)arg32);
		break;
	default:
		APS_ERR("%s not supported = 0x%04x", __func__, cmd);
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}
#endif

/*----------------------------------------------------------------------------*/
static struct file_operations stk3x1x_fops = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
	.owner = THIS_MODULE,
#endif
	.open = stk3x1x_open,
	.release = stk3x1x_release,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	.unlocked_ioctl = stk3x1x_unlocked_ioctl,
#else
	.ioctl = stk3x1x_ioctl,
#endif
#ifdef CONFIG_COMPAT
	.compat_ioctl = jsa1214_compat_ioctl,
#endif

};
/*----------------------------------------------------------------------------*/
static struct miscdevice stk3x1x_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "als_ps",
	.fops = &stk3x1x_fops,
};
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_HAS_EARLYSUSPEND
static int stk3x1x_i2c_suspend(struct i2c_client *client, pm_message_t msg)
{
	APS_FUN();
/*
	if(msg.event == PM_EVENT_SUSPEND)
	{
		if(!obj)
		{
			APS_ERR("null pointer!!\n");
			return -EINVAL;
		}

		atomic_set(&obj->als_suspend, 1);
		if((err = stk3x1x_enable_als(client, 0)))
		{
			APS_ERR("disable als: %d\n", err);
			return err;
		}

		atomic_set(&obj->ps_suspend, 1);
		if((err = stk3x1x_enable_ps(client, 0)))
		{
			APS_ERR("disable ps:  %d\n", err);
			return err;
		}

		stk3x1x_power(obj->hw, 0);
	}

*/
	return 0;
}
/*----------------------------------------------------------------------------*/
static int stk3x1x_i2c_resume(struct i2c_client *client)
{
	APS_FUN();
/*
	if(!obj)
	{
		APS_ERR("null pointer!!\n");
		return -EINVAL;
	}

	stk3x1x_power(obj->hw, 1);
	if((err = stk3x1x_init_client(client)))
	{
		APS_ERR("initialize client fail!!\n");
		return err;
	}
	atomic_set(&obj->als_suspend, 0);
	if(test_bit(STK_BIT_ALS, &obj->enable))
	{
		if((err = stk3x1x_enable_als(client, 1)))
		{
			APS_ERR("enable als fail: %d\n", err);
		}
	}
	atomic_set(&obj->ps_suspend, 0);
	if(test_bit(STK_BIT_PS,  &obj->enable))
	{
		if((err = stk3x1x_enable_ps(client, 1)))
		{
			APS_ERR("enable ps fail: %d\n", err);
		}
	}
*/
	return 0;
}
/*----------------------------------------------------------------------------*/
#else
static void stk3x1x_early_suspend(struct early_suspend *h)
{	/*early_suspend is only applied for ALS*/
	int err;
	struct stk3x1x_priv *obj = container_of(h, struct stk3x1x_priv, early_drv);
	int old = atomic_read(&obj->state_val);
	APS_FUN();

	if (!obj) {
		APS_ERR("null pointer!!\n");
		return;
	}

	if (old & STK_STATE_EN_ALS_MASK) {
		atomic_set(&obj->als_suspend, 1);
		err = stk3x1x_enable_als(obj->client, 0);
		if (err) {
			APS_ERR("disable als fail: %d\n", err);
		}
	}
}
/*----------------------------------------------------------------------------*/
static void stk3x1x_late_resume(struct early_suspend *h)
{	/*early_suspend is only applied for ALS*/
	int err;
	struct hwm_sensor_data sensor_data;
	struct stk3x1x_priv *obj = container_of(h, struct stk3x1x_priv, early_drv);

	memset(&sensor_data, 0, sizeof(sensor_data));
	APS_FUN();

	if (!obj) {
		APS_ERR("null pointer!!\n");
		return;
	}
	if (atomic_read(&obj->als_suspend)) {
		atomic_set(&obj->als_suspend, 0);
		if (test_bit(STK_BIT_ALS, &obj->enable)) {
			err = stk3x1x_enable_als(obj->client, 1);
			if (err) {
				APS_ERR("enable als fail: %d\n", err);

			}
		}
	}
}
#endif
int stk3x1x_ps_operate(void *self, uint32_t command, void *buff_in, int size_in,
					   void *buff_out, int size_out, int *actualout)
{
	int err = 0;
	int value;
	struct hwm_sensor_data *sensor_data;
	struct stk3x1x_priv *obj = (struct stk3x1x_priv *)self;

	return err;

	switch (command) {
	case SENSOR_DELAY:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			APS_ERR("Set delay parameter error!\n");
			err = -EINVAL;
		}
		break;

	case SENSOR_ENABLE:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			APS_ERR("Enable sensor parameter error!\n");
			err = -EINVAL;
		} else {
			value = *(int *)buff_in;
			if (value) {
				err = stk3x1x_enable_ps(obj->client, 1);
				if (err) {
					APS_ERR("enable ps fail: %d\n", err);
					return -1;
				}
				set_bit(STK_BIT_PS, &obj->enable);
			} else {
			err = stk3x1x_enable_ps(obj->client, 0);
				if (err) {
					APS_ERR("disable ps fail: %d\n", err);
					return -1;
				}
				clear_bit(STK_BIT_PS, &obj->enable);
			}
		}
		break;

	case SENSOR_GET_DATA:
		if ((buff_out == NULL) || (size_out < sizeof(sensor_data))) {
			APS_ERR("get sensor data parameter error!\n");
			err = -EINVAL;
		} else {
			sensor_data = (struct hwm_sensor_data *) buff_out;

			err = stk3x1x_read_ps(obj->client, &obj->ps);

			if (err) {
				err = -1;
			} else {
				value = stk3x1x_get_ps_value(obj, obj->ps);
				if (value < 0) {
					err = -1;
				} else {
					sensor_data->values[0] = value;
					sensor_data->value_divide = 1;
					sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
#ifdef STK_PS_POLLING_LOG
					APS_LOG("%s:ps raw 0x%x -> value 0x%x \n", __FUNCTION__, obj->ps, sensor_data->values[0]);
#endif
				}
			}
		}
		break;
	default:
		APS_ERR("proximity sensor operate function no this parameter %d!\n", command);
		err = -1;
		break;
	}

	return err;
}

int stk3x1x_als_operate(void *self, uint32_t command, void *buff_in, int size_in,
						void *buff_out, int size_out, int *actualout)
{
	int err = 0;
	int value;
	struct hwm_sensor_data *sensor_data;
	struct stk3x1x_priv *obj = (struct stk3x1x_priv *)self;
	u8 flag;

	switch (command) {
	case SENSOR_DELAY:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			APS_ERR("Set delay parameter error!\n");
			err = -EINVAL;
		}
		break;

	case SENSOR_ENABLE:
		if ((buff_in == NULL) || (size_in < sizeof(int))) {
			APS_ERR("Enable sensor parameter error!\n");
			err = -EINVAL;
		} else {
			value = *(int *)buff_in;
			if (value) {
				err = stk3x1x_enable_als(obj->client, 1);
				if (err) {
					APS_ERR("enable als fail: %d\n", err);
					return -1;
				}
				set_bit(STK_BIT_ALS, &obj->enable);
			} else {

			err = stk3x1x_enable_als(obj->client, 0);
				if (err) {
					APS_ERR("disable als fail: %d\n", err);
					return -1;
				}
				clear_bit(STK_BIT_ALS, &obj->enable);
			}

		}
		break;

	case SENSOR_GET_DATA:
		if ((buff_out == NULL) || (size_out < sizeof(sensor_data))) {
			APS_ERR("get sensor data parameter error!\n");
			err = -EINVAL;
		} else {
			err = stk3x1x_read_flag(obj->client, &flag);
			if (err)
				return err;

			if (!(flag & STK_FLG_ALSDR_MASK))
				return -1;

			sensor_data = (struct hwm_sensor_data *) buff_out;

			err = stk3x1x_read_als(obj->client, &obj->als);
			if (err) {
				err = -1;
			} else {
				sensor_data->values[0] = stk3x1x_get_als_value(obj, obj->als);
				sensor_data->value_divide = 1;
				sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
			}
		}
		break;
	default:
		APS_ERR("light sensor operate function no this parameter %d!\n", command);
		err = -1;
		break;
	}

	return err;
}

static int als_open_report_data(int open)
{
	return 0;
}

static int als_enable_nodata(int en)
{
	int res = 0;
#ifdef CUSTOM_KERNEL_SENSORHUB
	SCP_SENSOR_HUB_DATA req;
	int len;
#endif

	APS_LOG("stk3x1x_obj als enable value = %d\n", en);

#ifdef CUSTOM_KERNEL_SENSORHUB
	req.activate_req.sensorType = ID_LIGHT;
	req.activate_req.action = SENSOR_HUB_ACTIVATE;
	req.activate_req.enable = en;
	len = sizeof(req.activate_req);
	res = SCP_sensorHub_req_send(&req, &len, 1);
#else
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return -1;
	}
	res = stk3x1x_enable_als(stk3x1x_obj->client, en);
#endif
	if (res) {
		APS_ERR("als_enable_nodata is failed!!\n");
		return -1;
	}
	return 0;
}

static int als_set_delay(u64 ns)
{
	return 0;
}

static int als_get_data(int *value, int *status)
{
	int err = 0;
#ifdef CUSTOM_KERNEL_SENSORHUB
	SCP_SENSOR_HUB_DATA req;
	int len;
#else
	struct stk3x1x_priv *obj = NULL;
#endif

#ifdef CUSTOM_KERNEL_SENSORHUB
	req.get_data_req.sensorType = ID_LIGHT;
	req.get_data_req.action = SENSOR_HUB_GET_DATA;
	len = sizeof(req.get_data_req);
	err = SCP_sensorHub_req_send(&req, &len, 1);
	if (err) {
		APS_ERR("SCP_sensorHub_req_send fail!\n");
	} else {
		*value = req.get_data_rsp.int16_Data[0];
		*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	}

	if (atomic_read(&stk3x1x_obj->trace) & CMC_TRC_PS_DATA) {
		APS_LOG("value = %d\n", *value);
	}
#else
	if (!stk3x1x_obj) {
		APS_ERR("stk3x1x_obj is null!!\n");
		return -1;
	}
	obj = stk3x1x_obj;

	err = stk3x1x_read_als(obj->client, &obj->als);
	if (err) {
		err = -1;
	} else {
		*value = stk_alscode2lux(obj, obj->als);
		*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	}
#endif
	return err;
}


/*----------------------------------------------------------------------------*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
static int stk3x1x_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
	strcpy(info->type, stk3x1x_DEV_NAME);
	return 0;
}
#endif
/*----------------------------------------------------------------------------*/

static int reset_test_mode(struct i2c_client *client)
{
	u8 buf[2];
	u8 temp;
	int ret;

	buf[0] = 0x29;
	buf[1] = 0x00;

	ret = stk3x1x_master_send(client, 0x01, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	ret = stk3x1x_master_send(client, 0x0F, &buf[0], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_recv(client, 0x0E, &temp, 0x01);
	if (ret < 0) {
		APS_DBG("error: %d\n", ret);
		return -EFAULT;
	}
	ret = stk3x1x_master_send(client, 0x0F, &buf[1], 1);
	if (ret < 0) {
		APS_ERR("i2c write test reset error = %d\n", ret);
		return -EFAULT;
	}

	if (temp != 0x0) {
		ret = stk3x1x_master_send(client, 0x0F, &buf[0], 1);
		if (ret < 0) {
			APS_ERR("i2c write test reset error = %d\n", ret);
			return -EFAULT;
		}
		ret = stk3x1x_master_send(client, 0x0E, &buf[1], 1);
		if (ret < 0) {
			APS_ERR("i2c write test reset error = %d\n", ret);
			return -EFAULT;
		}
		ret = stk3x1x_master_send(client, 0x0F, &buf[1], 1);
		if (ret < 0) {
			APS_ERR("i2c write test reset error = %d\n", ret);
			return -EFAULT;
		}

	}

	msleep(1);
	return 0;
}

static int stk3x1x_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;
	struct stk3x1x_priv *obj;
	struct als_control_path als_ctl = {0};
	struct als_data_path als_data = {0};

	APS_LOG("%s: driver version: %s\n", __FUNCTION__, DRIVER_VERSION);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		goto exit;
	}
	stk3x1x_obj = obj;
	obj->hw = &alsps_cust;
	stk3x1x_get_addr(obj->hw, &obj->addr);

	INIT_DELAYED_WORK(&obj->eint_work, stk3x1x_eint_work);
	mutex_init(&obj->io_lock);
	obj->client = client;
	i2c_set_clientdata(client, obj);
	atomic_set(&obj->als_debounce, 200);
	atomic_set(&obj->als_deb_on, 0);
	atomic_set(&obj->als_deb_end, 0);
	atomic_set(&obj->ps_debounce, 100);
	atomic_set(&obj->ps_deb_on, 0);
	atomic_set(&obj->ps_deb_end, 0);
	atomic_set(&obj->ps_mask, 0);
	atomic_set(&obj->trace, 0x00);
	atomic_set(&obj->als_suspend, 0);

	atomic_set(&obj->state_val, 0x0);
	atomic_set(&obj->psctrl_val, PSCTRL_VAL);
	atomic_set(&obj->alsctrl_val, ALSCTRL_VAL);
	obj->ledctrl_val = LEDCTRL_VAL;
	obj->wait_val = WAIT_VAL;
	obj->int_val = 0;
	obj->first_boot = true;
	obj->als_correct_factor = 1000;
	obj->ps_cali = 0;

	atomic_set(&obj->ps_high_thd_val, obj->hw->ps_threshold_high);
	atomic_set(&obj->ps_low_thd_val, obj->hw->ps_threshold_low);

	atomic_set(&obj->recv_reg, 0);

	if (obj->hw->polling_mode_ps == 0) {
		APS_LOG("%s: enable PS interrupt\n", __FUNCTION__);
	}
	obj->int_val |= STK_INT_PS_MODE1;

	if (obj->hw->polling_mode_als == 0) {
		obj->int_val |= STK_INT_ALS;
		APS_LOG("%s: enable ALS interrupt\n", __FUNCTION__);
	}

	APS_LOG("%s: state_val=0x%x, psctrl_val=0x%x, alsctrl_val=0x%x, ledctrl_val=0x%x, wait_val=0x%x, int_val=0x%x\n",
			__FUNCTION__, atomic_read(&obj->state_val), atomic_read(&obj->psctrl_val), atomic_read(&obj->alsctrl_val),
			obj->ledctrl_val, obj->wait_val, obj->int_val);

	APS_LOG("stk3x1x_i2c_probe() OK!\n");
	obj->enable = 0;
	obj->pending_intr = 0;
	obj->als_level_num = sizeof(obj->hw->als_level)/sizeof(obj->hw->als_level[0]);
	obj->als_value_num = sizeof(obj->hw->als_value)/sizeof(obj->hw->als_value[0]);
	BUG_ON(sizeof(obj->als_level) != sizeof(obj->hw->als_level));
	memcpy(obj->als_level, obj->hw->als_level, sizeof(obj->als_level));
	BUG_ON(sizeof(obj->als_value) != sizeof(obj->hw->als_value));
	memcpy(obj->als_value, obj->hw->als_value, sizeof(obj->als_value));
	atomic_set(&obj->i2c_retry, 3);
	if (atomic_read(&obj->state_val) & STK_STATE_EN_ALS_MASK) {
		set_bit(STK_BIT_ALS, &obj->enable);
	}

	if (atomic_read(&obj->state_val) & STK_STATE_EN_PS_MASK) {
		set_bit(STK_BIT_PS, &obj->enable);
	}

	stk3x1x_i2c_client = client;

	err = reset_test_mode(client);
	err = stk3x1x_init_client(client);

	if (err) {
		goto exit_init_failed;
	}

	err = misc_register(&stk3x1x_device);
	if (err) {
		APS_ERR("stk3x1x_device register failed\n");
		goto exit_misc_device_register_failed;
	}
	als_ctl.is_use_common_factory = false;

	err = stk3x1x_create_attr(&(stk3x1x_init_info.platform_diver_addr->driver));
	if (err) {
		APS_ERR("create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}

	als_ctl.open_report_data = als_open_report_data;
	als_ctl.enable_nodata = als_enable_nodata;
	als_ctl.set_delay  = als_set_delay;
	als_ctl.is_report_input_direct = false;
#ifdef CUSTOM_KERNEL_SENSORHUB
	als_ctl.is_support_batch = obj->hw->is_batch_supported_als;
#else
	als_ctl.is_support_batch = false;
#endif

	err = als_register_control_path(&als_ctl);
	if (err) {
		APS_ERR("register fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	als_data.get_data = als_get_data;
	als_data.vender_div = 100;
	err = als_register_data_path(&als_data);
	if (err) {
		APS_ERR("tregister fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	err = batch_register_support_info(ID_LIGHT, als_ctl.is_support_batch, 100, 0);
	if (err) {
		APS_ERR("register light batch support err = %d\n", err);
	}
#if 0
	err = batch_register_support_info(ID_PROXIMITY, ps_ctl.is_support_batch, 100, 0);
	if (err) {
		APS_ERR("register proximity batch support err = %d\n", err);
	}
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND)
	obj->early_drv.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	obj->early_drv.suspend  = stk3x1x_early_suspend,
	obj->early_drv.resume   = stk3x1x_late_resume,
	register_early_suspend(&obj->early_drv);
#endif

	stk3x1x_init_flag = 0;
	APS_LOG("%s: OK\n", __FUNCTION__);
	return 0;

	exit_sensor_obj_attach_fail:
	exit_create_attr_failed:
	misc_deregister(&stk3x1x_device);
	exit_misc_device_register_failed:
	exit_init_failed:
	kfree(obj);
	obj = NULL;
	exit:
	stk3x1x_i2c_client = NULL;
	stk3x1x_init_flag = -1;
	APS_ERR("%s: err = %d\n", __FUNCTION__, err);
	return err;
}

static int stk3x1x_i2c_remove(struct i2c_client *client)
{
	int err;

	err = stk3x1x_delete_attr(&(stk3x1x_init_info.platform_diver_addr->driver));
	if (err) {
		APS_ERR("stk3x1x_delete_attr fail: %d\n", err);
	}

	err = misc_deregister(&stk3x1x_device);
	if (err) {
		APS_ERR("misc_deregister fail: %d\n", err);
	}

	stk3x1x_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));

	return 0;
}
/*----------------------------------------------------------------------------*/
static int stk3x1x_local_init(void)
{
	struct stk3x1x_i2c_addr addr;

	stk3x1x_power(hw, 1);
	stk3x1x_get_addr(hw, &addr);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
	stk3x1x_force[0] = hw->i2c_num;
	stk3x1x_force[1] = hw->i2c_addr[0];
#endif
	if (i2c_add_driver(&stk3x1x_i2c_driver)) {
		APS_ERR("add driver error\n");
		return -1;
	}
	if (-1 == stk3x1x_init_flag) {
		return -1;
	}
	als_cal = idme_get_alscal_value();
	if (als_cal > 0 && als_cal <= 65535) {
		g_stk3x1x_ptr->als_transmittance = als_cal;
	} else {
		g_stk3x1x_ptr->als_transmittance = Default_cali;
	}
	return 0;
}

static int stk3x1x_local_uninit(void)
{
	APS_FUN();
	stk3x1x_power(hw, 0);
	i2c_del_driver(&stk3x1x_i2c_driver);
	return 0;
}


/*----------------------------------------------------------------------------*/
static int __init stk3x1x_init(void)
{
	const char *name = "mediatek,jsa1214";
	hw = get_alsps_dts_func(name, hw);
	APS_LOG("%s: i2c_number=%d\n", __func__, hw->i2c_num);
	alsps_driver_add(&stk3x1x_init_info);
	return 0;
}

static void __exit stk3x1x_exit(void)
{
	APS_FUN();
}
/*----------------------------------------------------------------------------*/
module_init(stk3x1x_init);
module_exit(stk3x1x_exit);
/*----------------------------------------------------------------------------*/
MODULE_AUTHOR("MingHsien Hsieh");
MODULE_DESCRIPTION("SensorTek stk3x1x proximity and light sensor driver");
MODULE_LICENSE("GPL");
