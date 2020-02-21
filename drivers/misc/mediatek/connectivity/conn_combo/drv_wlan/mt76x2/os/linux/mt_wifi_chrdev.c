#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <linux/string.h>
#include <linux/of.h>

#include <mach/mt_boot.h>
#include <mach/mt_gpio.h>

MODULE_LICENSE("Dual BSD/GPL");

#define WIFI_DRIVER_NAME "mtk_WIFI_chrdev"
#define WIFI_NODE "wmtWifi"
#define WLAN_IFACE_NAME "wlan0"
#define WLAN_QUERYDEV_TIME 100

#define WIFI_DEV_MAJOR 0 /* never used number */

#define PFX                         "[MTK-WIFI] "
#define WIFI_LOG_DBG                  3
#define WIFI_LOG_INFO                 2
#define WIFI_LOG_WARN                 1
#define WIFI_LOG_ERR                  0


static unsigned int gDbgLevel = WIFI_LOG_INFO;

#define WIFI_DBG_FUNC(fmt, arg...) \
do { \
	if (gDbgLevel >= WIFI_LOG_DBG) \
		printk(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)

#define WIFI_INFO_FUNC(fmt, arg...) \
do { \
	if (gDbgLevel >= WIFI_LOG_INFO) \
		printk(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)

#define WIFI_WARN_FUNC(fmt, arg...) \
do { \
	if (gDbgLevel >= WIFI_LOG_WARN) \
		printk(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)

#define WIFI_ERR_FUNC(fmt, arg...) \
do { \
	if (gDbgLevel >= WIFI_LOG_ERR) \
		printk(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)

static int WIFI_devs = 1;        /* device count */
static int WIFI_major = WIFI_DEV_MAJOR;
struct class *pWIFIClass = NULL;
struct device *pWIFIDev = NULL;

module_param(WIFI_major, uint, 0);
static struct cdev WIFI_cdev;
static struct semaphore wr_mtx;
static int powered;
static int boot_rst;
static unsigned int g_wifi_rst_gpio;
/***************************************************************************
 *
 *	Platform HW Control
 *
 ***************************************************************************/

static unsigned int io_get_from_platform_data(void)
{
	struct device_node *io_node = NULL;
	int ret, num;
	io_node = of_find_compatible_node(NULL, NULL, "mediatek, mt76xx_wifi_ctrl");
	if (io_node) {
		ret = of_property_read_u32(io_node, "rst-gpio", &num);
		WIFI_ERR_FUNC("rst-gpio(%d)\n", num);
		if (!ret)
			return (unsigned int)num;
	} else
		WIFI_ERR_FUNC("%s: cannot find mt76xx_ctrl\n", __func__);
	return 0;
}

static int io_set_output_mode(unsigned int id)
{
	mt_set_gpio_pull_enable(id | 0x80000000, GPIO_PULL_DISABLE);
	mt_set_gpio_dir(id | 0x80000000, GPIO_DIR_OUT);
	mt_set_gpio_mode(id | 0x80000000, GPIO_MODE_GPIO);
	return 0;
}

static int io_set_output_low(unsigned int id)
{
	mt_set_gpio_out(id | 0x80000000, GPIO_OUT_ZERO);
	return 0;
}

static int io_set_output_high(unsigned int id)
{
	mt_set_gpio_out(id | 0x80000000, GPIO_OUT_ONE);
	return 0;
}

static int WIFI_func_ctrl(unsigned int on)
{
	BOOTMODE boot_mode = get_boot_mode();
	WIFI_ERR_FUNC("WIFI_func_ctrl GPIO_COMBO__EN_PIN(%d)\n", g_wifi_rst_gpio);
	if (on == 1) {

		io_set_output_mode(g_wifi_rst_gpio);
		io_set_output_high(g_wifi_rst_gpio);
		io_set_output_low(g_wifi_rst_gpio);
		mdelay(10);
		io_set_output_high(g_wifi_rst_gpio);

		boot_mode = get_boot_mode();
		if (boot_mode == FACTORY_BOOT) {
			WIFI_ERR_FUNC("Factory Boot(%d)\n", on);
			mdelay(5000);
		}
	} else if (on == 0) {
		if (boot_mode == FACTORY_BOOT) {
			WIFI_ERR_FUNC("Factory Boot(%d)\n", on);
			mdelay(500);
		}
	} else {
		WIFI_ERR_FUNC("Control Not support (%d)\n", on);
	}
	return 0;
}

/***************************************************************************
 *
 *	MTK-WIFI Device Operations
 *
 ***************************************************************************/
static int WIFI_open(struct inode *inode, struct file *file)
{
	WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
	imajor(inode),
	iminor(inode),
	current->pid
	);

	return 0;
}

static int WIFI_close(struct inode *inode, struct file *file)
{
	WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
	imajor(inode),
	iminor(inode),
	current->pid
	);

	return 0;
}

ssize_t WIFI_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int retval = -EIO;
	struct net_device *netdev = NULL;
	char local[12] = {0};
	int wait_cnt = 0;

	down(&wr_mtx);
	if (count <= 0) {
		WIFI_ERR_FUNC("WIFI_write invalid param\n");
		goto done;
	}

	if (0 == copy_from_user(local, buf, (count > sizeof(local)) ? sizeof(local) : count)) {
		local[11] = 0;
		WIFI_INFO_FUNC("WIFI_write %s\n", local);
		if (local[0] == '0') {
			if (powered == 0) {
				WIFI_INFO_FUNC("WIFI is already power off!\n");
				retval = count;
				goto done;
			} else {
				/* WIFI FUNCTION OFF */
				WIFI_func_ctrl(0);
				WIFI_INFO_FUNC("WMT turn off WIFI OK!\n");
				powered = 0;
				retval = count;
			}
		} else if (local[0] == '1') {
			/* WIFI FUNCTION ON */
			if (powered == 1) {
				WIFI_INFO_FUNC("WIFI is already power on!\n");
				retval = count;
				goto done;
			} else {
				netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
				if (boot_rst) {
					if (!netdev)
						WIFI_func_ctrl(1);
					else {
						WIFI_INFO_FUNC("wlan0 net device existed\n");
						dev_put(netdev);
						netdev = NULL;
					}
				} else {
					WIFI_func_ctrl(1); /* rst once after boot */
					while (netdev != NULL && wait_cnt < 100) { /* check RST success */
						dev_put(netdev);
						netdev = NULL;
						msleep(WLAN_QUERYDEV_TIME);
						wait_cnt++;
						netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
					}
					boot_rst = 1;
				}
				WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
				powered = 1;
				retval = count;
			}
		} else if (local[0] == 'S' || local[0] == 'P' || local[0] == 'A') {
			netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
			if (powered == 0) {
				if (!netdev)
					WIFI_func_ctrl(1);
				WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
				powered = 1;
			}

			while (netdev == NULL && wait_cnt < 100) {
				WIFI_ERR_FUNC("Fail to get wlan0, sleep %d ms(%d)\n", WLAN_QUERYDEV_TIME, wait_cnt);
				msleep(WLAN_QUERYDEV_TIME);
				wait_cnt++;
				netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
			}
			if (wait_cnt >= 100) {
				WIFI_ERR_FUNC("Get wlan0 net device timeout\n");
				goto done;
			}
			WIFI_INFO_FUNC("wlan0 net device created\n");
			dev_put(netdev);
			netdev = NULL;
		}
	}

done:
	if (netdev != NULL)
		dev_put(netdev);
	up(&wr_mtx);
	return retval;
}


const struct file_operations WIFI_fops = {
	.open = WIFI_open,
	.release = WIFI_close,
	.write = WIFI_write,
};

static int WIFI_init(void)
{
	dev_t devID = MKDEV(WIFI_major, 0);
	int alloc_ret = 0;
	int cdev_err = 0;

	/*static allocate chrdev*/
	alloc_ret = alloc_chrdev_region(&devID, 0, 1, WIFI_DRIVER_NAME);
	if (alloc_ret) {
		WIFI_ERR_FUNC("fail to allocate chrdev\n");
		return alloc_ret;
	}

	cdev_init(&WIFI_cdev, &WIFI_fops);
	WIFI_cdev.owner = THIS_MODULE;

	cdev_err = cdev_add(&WIFI_cdev, devID, WIFI_devs);
	if (cdev_err)
		goto error;

	pWIFIClass = class_create(THIS_MODULE, WIFI_DRIVER_NAME);
	if (IS_ERR(pWIFIClass)) {
		WIFI_ERR_FUNC("class create fail, error code(%ld)\n", PTR_ERR(pWIFIClass));
		goto err1;
	}

	pWIFIDev = device_create(pWIFIClass, NULL, devID, NULL, WIFI_NODE);
	if (IS_ERR(pWIFIDev)) {
		WIFI_ERR_FUNC("device create fail, error code(%ld)\n", PTR_ERR(pWIFIDev));
		goto err2;
	}
	sema_init(&wr_mtx, 1);

	WIFI_INFO_FUNC("%s driver(major %d) installed.\n", WIFI_DRIVER_NAME, WIFI_major);

	g_wifi_rst_gpio = io_get_from_platform_data();
	return 0;
err2:
	if (pWIFIClass) {
		class_destroy(pWIFIClass);
		pWIFIClass = NULL;
	}

err1:
error:
	if (cdev_err == 0)
		cdev_del(&WIFI_cdev);

	if (alloc_ret == 0)
		unregister_chrdev_region(devID, WIFI_devs);

	return -1;
}

static void WIFI_exit(void)
{
	dev_t dev = MKDEV(WIFI_major, 0);
	if (pWIFIDev) {
		device_destroy(pWIFIClass, dev);
		pWIFIDev = NULL;
	}

	if (pWIFIClass) {
		class_destroy(pWIFIClass);
		pWIFIClass = NULL;
	}

	cdev_del(&WIFI_cdev);
	unregister_chrdev_region(dev, WIFI_devs);

	WIFI_INFO_FUNC("%s driver removed.\n", WIFI_DRIVER_NAME);
}

module_init(WIFI_init);
module_exit(WIFI_exit);
