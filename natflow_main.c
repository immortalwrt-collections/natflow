/*
 * Author: Chen Minqiang <ptpt52@gmail.com>
 *  Date : Fri, 11 May 2018 14:20:51 +0800
 */
#include <linux/ctype.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/tcp.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/version.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/highmem.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include "natflow.h"
#include "natflow_common.h"
#if defined(CONFIG_NATFLOW_PATH)
#include "natflow_path.h"
#endif
#include "natflow_zone.h"
#include "natflow_user.h"
#if defined(CONFIG_NATFLOW_URLLOGGER)
#include "natflow_urllogger.h"
#endif
#include "natflow_conntrack.h"

static int natflow_major = 0;
static int natflow_minor = 0;
static int number_of_devices = 1;
static struct cdev natflow_cdev;
const char *natflow_dev_name = "natflow_ctl";
static struct class *natflow_class;
static struct device *natflow_dev;

static void *natflow_start(struct seq_file *m, loff_t *pos)
{
	int n = 0;
	char *natflow_ctl_buffer = m->private;
#if defined(CONFIG_NATFLOW_PATH)
	unsigned long *lpos = (unsigned long *)natflow_ctl_buffer;
#endif

	natflow_ctl_buffer = natflow_ctl_buffer + sizeof(unsigned long);

	if ((*pos) == 0) {
		n = snprintf(natflow_ctl_buffer,
		             PAGE_SIZE - 1 - sizeof(unsigned long),
		             "# Version: %s\n"
		             "# Usage:\n"
		             "#    disabled=Number -- set disable/enable\n"
		             "#    debug=<num> -- set debug\n"
		             "#    ifname_group_type=<num> -- set ifname_group_type\n"
		             "#    ifname_group_clear -- clear all ifname_group\n"
		             "#    ifname_group_add=<ifname> -- add ifname to filter\n"
		             "#    vline_clear -- clear vline\n"
		             "#    vline_add=<ifname>,<ifname>,<family> -- add vline <family>=ipv4/ipv6/all\n"
		             "#    vline_apply -- apply vline config\n"
		             "#\n"
#if defined(CONFIG_HWNAT_EXTDEV_USE_VLAN_HASH)
		             "# Info: CONFIG_HWNAT_EXTDEV_USE_VLAN_HASH\n"
#else
		             "# Info:\n"
#endif
		             "#    ...\n"
#if defined(CONFIG_NATFLOW_PATH)
#if (defined(CONFIG_NET_RALINK_OFFLOAD) || defined(NATFLOW_OFFLOAD_HWNAT_FAKE) && defined(CONFIG_NET_MEDIATEK_SOC))
		             "#    hwnat=%u\n"
#if defined(CONFIG_NET_MEDIATEK_SOC_WED)
		             "#    hwnat_wed_disabled=%u\n"
#endif
#endif
		             "#    delay_pkts=%u\n"
		             "#    go_slowpath_if_no_qos=%u\n"
#endif
		             "#\n"
		             "# Reload cmd:\n"
		             "\n"
#if defined(CONFIG_NATFLOW_PATH)
		             "disabled=%u\n"
		             "ifname_group_type=%u\n"
#endif
		             "debug=%d\n"
		             "\n",
		             NATFLOW_VERSION,
#if defined(CONFIG_NATFLOW_PATH)
#if (defined(CONFIG_NET_RALINK_OFFLOAD) || defined(NATFLOW_OFFLOAD_HWNAT_FAKE) && defined(CONFIG_NET_MEDIATEK_SOC))
		             hwnat,
#if defined(CONFIG_NET_MEDIATEK_SOC_WED)
		             hwnat_wed_disabled,
#endif
#endif
		             delay_pkts,
		             go_slowpath_if_no_qos,
		             natflow_disabled_get(),
		             ifname_group_type,
#endif
		             debug);
		natflow_ctl_buffer[n] = 0;
		return natflow_ctl_buffer;
	}
#if defined(CONFIG_NATFLOW_PATH)
	else if ((*pos) > 0) {
		struct net_device *dev = ifname_group_get((*pos) - 1);
		if (dev) {
			*lpos = (*pos);
			n = snprintf(natflow_ctl_buffer,
			             PAGE_SIZE - 1 - sizeof(unsigned long),
			             "ifname_group_add=%s\n",
			             dev->name);
			natflow_ctl_buffer[n] = 0;
			dev_put(dev);
			return natflow_ctl_buffer;
		} else {
			unsigned char family = 0;
			unsigned char (*ptr)[2][IFNAMSIZ];
			if (*pos == 1)
				*lpos = 0;
			ptr = vline_fwd_map_config_get((*pos) - (*lpos) - 1, &family);
			if (ptr) {
				n = snprintf(natflow_ctl_buffer,
				             PAGE_SIZE - 1 - sizeof(unsigned long),
				             "vline_add=%s,%s,%s\n",
				             (*ptr)[0], (*ptr)[1], family == VLINE_FAMILY_IPV4 ? "ipv4" : family == VLINE_FAMILY_IPV6 ? "ipv6" : "all");
				natflow_ctl_buffer[n] = 0;
				return natflow_ctl_buffer;
			}
		}
	}
#endif

	return NULL;
}

static void *natflow_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	if ((*pos) > 0) {
		return natflow_start(m, pos);
	}
	return NULL;
}

static void natflow_stop(struct seq_file *m, void *v)
{
}

static int natflow_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s", (char *)v);
	return 0;
}

const struct seq_operations natflow_seq_ops = {
	.start = natflow_start,
	.next = natflow_next,
	.stop = natflow_stop,
	.show = natflow_show,
};

static ssize_t natflow_read(struct file *file, char __user *buf, size_t buf_len, loff_t *offset)
{
	return seq_read(file, buf, buf_len, offset);
}

static ssize_t natflow_write(struct file *file, const char __user *buf, size_t buf_len, loff_t *offset)
{
	int err = 0;
	int n, l;
	int cnt = MAX_IOCTL_LEN;
	static char data[MAX_IOCTL_LEN];
	static int data_left = 0;

	cnt -= data_left;
	if (buf_len < cnt)
		cnt = buf_len;

	if (copy_from_user(data + data_left, buf, cnt) != 0)
		return -EACCES;

	n = 0;
	while(n < cnt && (data[n] == ' ' || data[n] == '\n' || data[n] == '\t')) n++;
	if (n) {
		*offset += n;
		data_left = 0;
		return n;
	}

	//make sure line ended with '\n' and line len <= MAX_IOCTL_LEN
	l = 0;
	while (l < cnt && data[l + data_left] != '\n') l++;
	if (l >= cnt) {
		data_left += l;
		if (data_left >= MAX_IOCTL_LEN) {
			NATFLOW_println("err: too long a line");
			data_left = 0;
			return -EINVAL;
		}
		goto done;
	} else {
		data[l + data_left] = '\0';
		data_left = 0;
		l++;
	}

	if (strncmp(data, "debug=", 6) == 0) {
		int d;
		n = sscanf(data, "debug=%u", &d);
		if (n == 1) {
			debug = d;
			goto done;
		}
#if defined(CONFIG_NATFLOW_PATH)
	} else if (strncmp(data, "disabled=", 9) == 0) {
		int d;
		n = sscanf(data, "disabled=%u", &d);
		if (n == 1) {
			natflow_disabled_set(!!d);
			goto done;
		}
#if (defined(CONFIG_NET_RALINK_OFFLOAD) || defined(NATFLOW_OFFLOAD_HWNAT_FAKE) && defined(CONFIG_NET_MEDIATEK_SOC))
	} else if (strncmp(data, "hwnat=", 6) == 0) {
		int d;
		n = sscanf(data, "hwnat=%u", &d);
		if (n == 1) {
			hwnat = d;
			goto done;
		}
#if defined(CONFIG_NET_MEDIATEK_SOC_WED)
	} else if (strncmp(data, "hwnat_wed_disabled=", 19) == 0) {
		int d;
		n = sscanf(data, "hwnat_wed_disabled=%u", &d);
		if (n == 1) {
			hwnat_wed_disabled = d;
			goto done;
		}
#endif
#endif
	} else if (strncmp(data, "delay_pkts=", 11) == 0) {
		int d;
		n = sscanf(data, "delay_pkts=%u", &d);
		if (n == 1) {
			delay_pkts = d;
			goto done;
		}
	} else if (strncmp(data, "go_slowpath_if_no_qos=", 22) == 0) {
		int d;
		n = sscanf(data, "go_slowpath_if_no_qos=%u", &d);
		if (n == 1) {
			go_slowpath_if_no_qos = d;
			goto done;
		}
	} else if (strncmp(data, "ifname_group_type=", 18) == 0) {
		int d;
		n = sscanf(data, "ifname_group_type=%u", &d);
		if (n == 1) {
			ifname_group_type = d;
			goto done;
		}
	} else if (strncmp(data, "ifname_group_clear", 18) == 0) {
		ifname_group_clear();
		goto done;
	} else if (strncmp(data, "ifname_group_add=", 17) == 0) {
		char *ifname = NULL;
		ifname = kmalloc(2048, GFP_KERNEL);
		if (!ifname)
			return -ENOMEM;
		n = sscanf(data, "ifname_group_add=%s\n", ifname);
		if (n == 1) {
			err = ifname_group_add(ifname);
			if (err == 0) {
				kfree(ifname);
				goto done;
			}
		}
		kfree(ifname);
	} else if (strncmp(data, "vline_clear", 11) == 0) {
		vline_fwd_map_config_clear();
		goto done;
	} else if (strncmp(data, "vline_add=", 10) == 0) {
		char str[3][16];
		n = sscanf(data, "vline_add=%15[^,],%15[^,],%4[^,]\n", str[0], str[1], str[2]);
		if (n == 3) {
			unsigned char family = 255;
			if ((memcmp(str[2], "ipv4", 5) == 0)) {
				family = VLINE_FAMILY_IPV4;
			} else if ((memcmp(str[2], "ipv6", 5) == 0)) {
				family = VLINE_FAMILY_IPV6;
			} else if ((memcmp(str[2], "all", 4) == 0)) {
				family = VLINE_FAMILY_ALL;
			}
			if (family != 255) {
				err = vline_fwd_map_config_add(str[1], str[0], family);
				if (err == 0) {
					goto done;
				}
			}
			err = -EINVAL;
		}
	} else if (strncmp(data, "vline_apply", 11) == 0) {
		err = vline_fwd_map_config_apply();
		if (err == 0) {
			goto done;
		}
	} else if (strncmp(data, "update_magic", 12) == 0) {
		natflow_update_magic(0);
		goto done;
#endif
	}

	NATFLOW_println("ignoring line[%s]", data);
	if (err != 0) {
		return err;
	}

done:
	*offset += l;
	return l;
}

static int natflow_open(struct inode *inode, struct file *file)
{
	int ret;
	//set nonseekable
	file->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);

	ret = seq_open_private(file, &natflow_seq_ops, PAGE_SIZE);
	if (ret)
		return ret;

	return 0;
}

static int natflow_release(struct inode *inode, struct file *file)
{
	int ret = seq_release_private(inode, file);
	return ret;
}

static struct file_operations natflow_fops = {
	.owner = THIS_MODULE,
	.open = natflow_open,
	.release = natflow_release,
	.read = natflow_read,
	.write = natflow_write,
	.llseek  = seq_lseek,
};

static int __init natflow_init(void) {
	int retval = 0;
	dev_t devno;

	if (natflow_major > 0) {
		devno = MKDEV(natflow_major, natflow_minor);
		retval = register_chrdev_region(devno, number_of_devices, natflow_dev_name);
	} else {
		retval = alloc_chrdev_region(&devno, natflow_minor, number_of_devices, natflow_dev_name);
	}
	if (retval < 0) {
		NATFLOW_println("alloc_chrdev_region failed!");
		return retval;
	}
	natflow_major = MAJOR(devno);
	natflow_minor = MINOR(devno);
	NATFLOW_println("natflow_major=%d, natflow_minor=%d", natflow_major, natflow_minor);

	cdev_init(&natflow_cdev, &natflow_fops);
	natflow_cdev.owner = THIS_MODULE;
	natflow_cdev.ops = &natflow_fops;

	retval = cdev_add(&natflow_cdev, devno, 1);
	if (retval) {
		NATFLOW_println("adding chardev, error=%d", retval);
		goto cdev_add_failed;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	natflow_class = class_create(THIS_MODULE, "natflow_class");
#else
	natflow_class = class_create("natflow_class");
#endif
	if (IS_ERR(natflow_class)) {
		NATFLOW_println("failed in creating class");
		retval = -EINVAL;
		goto class_create_failed;
	}

	natflow_dev = device_create(natflow_class, NULL, devno, NULL, natflow_dev_name);
	if (IS_ERR(natflow_dev)) {
		retval = -EINVAL;
		goto device_create_failed;
	}

	retval = natflow_zone_init();
	if (retval) {
		NATFLOW_println("natflow_zone_init fail, error=%d", retval);
		goto natflow_zone_init_failed;
	}

	retval = natflow_user_init();
	if (retval) {
		NATFLOW_println("natflow_user_init fail, error=%d", retval);
		goto natflow_user_init_failed;
	}

	retval = conntrackinfo_init();
	if (retval) {
		NATFLOW_println("conntrackinfo_init fail, error=%d", retval);
		goto conntrackinfo_init_failed;
	}

#if defined(CONFIG_NATFLOW_PATH)
	retval = natflow_path_init();
	if (retval) {
		NATFLOW_println("natflow_path_init fail, error=%d", retval);
		goto natflow_path_init_failed;
	}
#endif

#if defined(CONFIG_NATFLOW_URLLOGGER)
	retval = natflow_urllogger_init();
	if (retval) {
		NATFLOW_println("natflow_urllogger_init fail, error=%d", retval);
		goto natflow_urllogger_init_failed;
	}
#endif

	return 0;

#if defined(CONFIG_NATFLOW_URLLOGGER)
	//natflow_urllogger_exit();
natflow_urllogger_init_failed:
#endif
#if defined(CONFIG_NATFLOW_PATH)
	natflow_path_exit();
natflow_path_init_failed:
#endif
	conntrackinfo_exit();
conntrackinfo_init_failed:
	natflow_user_exit();
natflow_user_init_failed:
	natflow_zone_exit();
natflow_zone_init_failed:
	device_destroy(natflow_class, devno);
device_create_failed:
	class_destroy(natflow_class);
class_create_failed:
	cdev_del(&natflow_cdev);
cdev_add_failed:
	unregister_chrdev_region(devno, number_of_devices);

	return retval;
}

static void __exit natflow_exit(void) {
	dev_t devno;

	NATFLOW_println("removing");

#if defined(CONFIG_NATFLOW_URLLOGGER)
	natflow_urllogger_exit();
#endif
#if defined(CONFIG_NATFLOW_PATH)
	natflow_path_exit();
#endif
	conntrackinfo_exit();
	natflow_user_exit();
	natflow_zone_exit();

	devno = MKDEV(natflow_major, natflow_minor);
	device_destroy(natflow_class, devno);
	class_destroy(natflow_class);
	cdev_del(&natflow_cdev);
	unregister_chrdev_region(devno, number_of_devices);
	NATFLOW_println("done");
	return;
}

module_init(natflow_init);
module_exit(natflow_exit);

MODULE_AUTHOR("Q2hlbiBNaW5xaWFuZyA8cHRwdDUyQGdtYWlsLmNvbT4=");
MODULE_VERSION(NATFLOW_VERSION);
MODULE_DESCRIPTION("NATFLOW fast forward module");
MODULE_LICENSE("GPL");
