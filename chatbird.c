/*
 * Chatbird USB driver
 *
 * Copyright (C) 2022 Peter-Simon Dieterich <dieterich.peter@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 * derived from Dream Cheeky USB Missile Launcher driver
 * Copyright (C) 2007 Matthias Vallentin <vallentin@icsi.berkeley.edu>
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/slab.h>	 /* kmalloc() */
#include <linux/usb.h>	 /* USB stuff */
#include <linux/mutex.h> /* mutexes */
#include <linux/ioctl.h>

#include <linux/uaccess.h> /* copy_*_user */

#include <linux/errno.h>

#define CHATBIRD_CTRL_BUFFER_SIZE 8
#define CHATBIRD_CTRL_REQUEST_TYPE 0b01000000
#define CHATBIRD_CTRL_REQUEST 0x0
#define CHATBIRD_CTRL_VALUE 0x0
#define CHATBIRD_CTRL_INDEX 0x0

#define CHATBIRD_INT_OUT_BUFFER_SIZE 4096

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define CHATBIRD_MINOR_BASE 0
#else
#define CHATBIRD_MINOR_BASE 96
#endif

#define DEBUG_LEVEL_DEBUG 0x1F
#define DEBUG_LEVEL_INFO 0x0F
#define DEBUG_LEVEL_WARN 0x07
#define DEBUG_LEVEL_ERROR 0x03
#define DEBUG_LEVEL_CRITICAL 0x01

#define DBG_DEBUG(fmt, args...)                                     \
	if ((debug_level & DEBUG_LEVEL_DEBUG) == DEBUG_LEVEL_DEBUG) \
	printk(KERN_DEBUG "[debug] %s(%d): " fmt "\n",              \
	       __FUNCTION__, __LINE__, ##args)
#define DBG_INFO(fmt, args...)                                    \
	if ((debug_level & DEBUG_LEVEL_INFO) == DEBUG_LEVEL_INFO) \
	printk(KERN_DEBUG "[info]  %s(%d): " fmt "\n",            \
	       __FUNCTION__, __LINE__, ##args)
#define DBG_WARN(fmt, args...)                                    \
	if ((debug_level & DEBUG_LEVEL_WARN) == DEBUG_LEVEL_WARN) \
	printk(KERN_DEBUG "[warn]  %s(%d): " fmt "\n",            \
	       __FUNCTION__, __LINE__, ##args)
#define DBG_ERR(fmt, args...)                                       \
	if ((debug_level & DEBUG_LEVEL_ERROR) == DEBUG_LEVEL_ERROR) \
	printk(KERN_DEBUG "[err]   %s(%d): " fmt "\n",              \
	       __FUNCTION__, __LINE__, ##args)
#define DBG_CRIT(fmt, args...)                                            \
	if ((debug_level & DEBUG_LEVEL_CRITICAL) == DEBUG_LEVEL_CRITICAL) \
	printk(KERN_DEBUG "[crit]  %s(%d): " fmt "\n",                    \
	       __FUNCTION__, __LINE__, ##args)

struct usb_chatbird
{
	struct usb_device *udev;
	struct usb_interface *interface;
	unsigned char minor;
	char serial_number[8];

	int open_count;		 /* Open count for this port */
	struct semaphore sem;	 /* Locks this structure */
	spinlock_t cmd_spinlock; /* locks dev->command */

	char *int_out_buffer;
	struct usb_endpoint_descriptor *int_out_endpoint;
	struct urb *int_out_urb;
	int int_out_running;

	char *int_in_buffer;
	struct usb_endpoint_descriptor *int_in_endpoint;
	struct urb *int_in_urb;
	int int_in_running;

	char *ctrl_buffer; /* 8 byte buffer for ctrl msg */
	struct urb *ctrl_urb;
	struct usb_ctrlrequest *ctrl_dr; /* Setup packet information */

	// int			correction_required;
	// __u8			command;/* Last issued command */
};

static struct usb_device_id chatbird_table[] =
    {
	{USB_DEVICE(0x03ee, 0xff01)},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, chatbird_table);

static int debug_level = DEBUG_LEVEL_INFO;
static int debug_trace = 0;
module_param(debug_level, int, S_IRUGO | S_IWUSR);
module_param(debug_trace, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_level, "debug level (bitmask)");
MODULE_PARM_DESC(debug_trace, "enable function tracing");

/* Prevent races between open() and disconnect */
static DEFINE_MUTEX(disconnect_mutex);
static struct usb_driver chatbird_driver;

static inline void chatbird_debug_data(const char *function, int size,
				       const unsigned char *data)
{
	int i;
	if ((debug_level & DEBUG_LEVEL_DEBUG) == DEBUG_LEVEL_DEBUG)
	{
		printk(KERN_DEBUG "[debug] %s: length = %d, data = ",
		       function, size);
		for (i = 0; i < size; ++i)
			printk("%.2x ", data[i]);
		printk("\n");
	}
}

static void chatbird_abort_transfers(struct usb_chatbird *dev)
{
	if (!dev)
	{
		DBG_ERR("dev is NULL");
		return;
	}

	if (!dev->udev)
	{
		DBG_ERR("udev is NULL");
		return;
	}

	if (dev->udev->state == USB_STATE_NOTATTACHED)
	{
		DBG_ERR("udev not attached");
		return;
	}

	/* Shutdown transfer */
	if (dev->int_in_running)
	{
		dev->int_in_running = 0;
		mb();
		if (dev->int_in_urb)
			usb_kill_urb(dev->int_in_urb);
	}

	if (dev->ctrl_urb)
		usb_kill_urb(dev->ctrl_urb);
}

static inline void chatbird_delete(struct usb_chatbird *dev)
{
	chatbird_abort_transfers(dev);

	/* Free data structures. */
	if (dev->int_in_urb)
		usb_free_urb(dev->int_in_urb);
	if (dev->int_out_urb)
		usb_free_urb(dev->int_out_urb);
	if (dev->ctrl_urb)
		usb_free_urb(dev->ctrl_urb);

	kfree(dev->int_in_buffer);
	kfree(dev->int_out_buffer);
	kfree(dev->ctrl_buffer);
	kfree(dev->ctrl_dr);
	kfree(dev);
}

static void chatbird_ctrl_callback(struct urb *urb)
{
	// struct usb_chatbird *dev = urb->context;
	DBG_INFO("ctrl callback %d", urb->status);
}

// static void chatbird_int_out_callback(struct urb *urb) {
// 	DBG_INFO("int_out callback %d", urb->status);
// }

static void chatbird_int_in_callback(struct urb *urb)
{
	struct usb_chatbird *dev = urb->context;
	int retval;

	DBG_INFO("int_in callback %d", urb->status);

	chatbird_debug_data(__FUNCTION__, urb->actual_length, urb->transfer_buffer);

	if (urb->status)
	{
		if (urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)
		{
			return;
		}
		else
		{
			DBG_ERR("non-zero urb status (%d)", urb->status);
			goto resubmit; /* Maybe we can recover. */
		}
	}

	/* Handle key events here */

resubmit:
	/* Resubmit if we're still running. */
	if (dev->int_in_running && dev->udev)
	{
		retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);
		if (retval)
		{
			DBG_ERR("resubmitting urb failed (%d)", retval);
			dev->int_in_running = 0;
		}
	}
}

static ssize_t chatbird_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	return -1;
}

static long chatbird_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usb_chatbird *dev;
	int retval = 0;
	u16 value = 0;
	u16 index;

	DBG_INFO("unlocked_ioctl %d %ld", cmd, arg);

	dev = file->private_data;
	/* Lock this object. */
	if (down_interruptible(&dev->sem))
	{
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* Verify that the device wasn't unplugged. */
	if (!dev->udev)
	{
		retval = -ENODEV;
		DBG_ERR("No device or device unplugged (%d)", retval);
		goto unlock_exit;
	}

	if (cmd != 0)
	{
		retval = -1;
		goto unlock_exit;
	}

	index = 5000; // 0b1001110001000
	// 0xaf05 ?
	// 0xbc00
	switch (arg)
	{
	case '0':		// ??
		value = 0xbc00; // 0b1011110000000000
		break;
	case '1':		// all off
		value = 0xbc01; // 0b1011110000000001
		break;
	case '2':		// both leds on
		value = 0xbcc1; // 0b1011110011000001
		break;
	case '3':		// flap wings
		value = 0xbc05; // 0b1011110000000101
		break;
	case '4':		// move beak
		value = 0xbc03; // 0b1011110000000011
		break;
	case '5':		// tilt head
		value = 0xbc04; // 0b1011110000000100
		break;
	case '6':		// flap once
		index = 55;	// 0b110111
		value = 0xaf05; // 0b1010111100000101
		break;
	default:
		value = 0xbc00;
		break;
	}

	retval = usb_control_msg(dev->udev,
				 usb_sndctrlpipe(dev->udev, 0),
				 0x1,	     // request
				 0b01000000, // bRequestType
				 value,	     // value
				 index,	     // index
				 0,	     // data
				 0,	     // size
				 HZ * 5	     // timeout
	);

	if (retval < 0)
	{
		DBG_ERR("usb_control_msg failed (%d)", retval);
	}

unlock_exit:
	up(&dev->sem);

exit:
	return retval;
}

static ssize_t chatbird_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct usb_chatbird *dev;
	int retval = 0;
	int actual_length = 0;

	dev = file->private_data;

	/* Lock this object. */
	if (down_interruptible(&dev->sem))
	{
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* Verify that the device wasn't unplugged. */
	if (!dev->udev)
	{
		retval = -ENODEV;
		DBG_ERR("No device or device unplugged (%d)", retval);
		goto unlock_exit;
	}

	/* Verify that we actually have some data to write. */
	if (count == 0)
		goto unlock_exit;

	if (count > CHATBIRD_INT_OUT_BUFFER_SIZE)
		count = CHATBIRD_INT_OUT_BUFFER_SIZE;

	/* Copy data from user into kernel memory */
	if (copy_from_user(dev->int_out_buffer, user_buf, count))
	{
		retval = -EFAULT;
		goto unlock_exit;
	}

	DBG_INFO("Got data of length %ld", count);

	// usb_fill_int_urb(
	// 	dev->int_out_urb,
	// 	dev->udev,
	// 	usb_sndintpipe(dev->udev, dev->int_out_endpoint->bEndpointAddress), //->desc.bEndpointAddress),
	// 	dev->int_out_buffer,
	// 	sizeof(dev->int_out_buffer),
	// 	chatbird_int_in_callback,
	// 	0,
	// 	dev->int_out_endpoint->bInterval
	// );
	// retval = usb_submit_urb(dev->int_out_urb, GFP_KERNEL);
	// goto error_check;

	retval = usb_interrupt_msg(
	    dev->udev,
	    usb_sndintpipe(dev->udev, dev->int_out_endpoint->bEndpointAddress),
	    dev->int_out_buffer,
	    count,
	    &actual_length,
	    500);

	DBG_INFO("wrote %d retval %d", actual_length, retval);

	if (retval < 0)
	{
		DBG_ERR("usb_control_msg failed (%d)", retval);
		goto unlock_exit;
	}

	retval = actual_length;

unlock_exit:
	up(&dev->sem);

exit:
	return retval;
}

static int chatbird_open(struct inode *inode, struct file *file)
{
	struct usb_chatbird *dev = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	DBG_INFO("Open device");
	subminor = iminor(inode);

	mutex_lock(&disconnect_mutex);

	interface = usb_find_interface(&chatbird_driver, subminor);
	if (!interface)
	{
		DBG_ERR("can't find device for minor %d", subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev)
	{
		retval = -ENODEV;
		goto exit;
	}

	/* lock this device */
	if (down_interruptible(&dev->sem))
	{
		DBG_ERR("sem down failed");
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* Increment our usage count for the device. */
	++dev->open_count;
	if (dev->open_count > 1)
		DBG_DEBUG("open_count = %d", dev->open_count);

	/* Initialize interrupt URB. */
	usb_fill_int_urb(dev->int_in_urb, dev->udev,
			 usb_rcvintpipe(dev->udev,
					dev->int_in_endpoint->bEndpointAddress),
			 dev->int_in_buffer,
			 le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize),
			 chatbird_int_in_callback,
			 dev,
			 dev->int_in_endpoint->bInterval);

	dev->int_in_running = 1;
	mb();

	retval = usb_submit_urb(dev->int_in_urb, GFP_KERNEL);
	if (retval)
	{
		DBG_ERR("submitting int urb failed (%d)", retval);
		dev->int_in_running = 0;
		--dev->open_count;
		goto unlock_exit;
	}

	/* Save our object in the file's private structure. */
	file->private_data = dev;

unlock_exit:
	up(&dev->sem);

exit:
	mutex_unlock(&disconnect_mutex);
	DBG_INFO("open finished: %d", retval);
	return retval;
}
static int chatbird_release(struct inode *inode, struct file *file)
{
	struct usb_chatbird *dev = NULL;
	int retval = 0;

	DBG_INFO("Release driver");
	dev = file->private_data;

	if (!dev)
	{
		DBG_ERR("dev is NULL");
		retval = -ENODEV;
		goto exit;
	}

	/* Lock our device */
	if (down_interruptible(&dev->sem))
	{
		retval = -ERESTARTSYS;
		goto exit;
	}

	if (dev->open_count <= 0)
	{
		DBG_ERR("device not opened");
		retval = -ENODEV;
		goto unlock_exit;
	}

	if (!dev->udev)
	{
		DBG_DEBUG("device unplugged before the file was released");
		up(&dev->sem); /* Unlock here as ml_delete frees dev. */
		chatbird_delete(dev);
		goto exit;
	}

	if (dev->open_count > 1)
		DBG_DEBUG("open_count = %d", dev->open_count);

	chatbird_abort_transfers(dev);
	--dev->open_count;

unlock_exit:
	up(&dev->sem);

exit:
	return retval;
}

static struct file_operations chatbird_fops = {
    .owner = THIS_MODULE,
    .write = chatbird_write,
    .read = chatbird_read,
    .open = chatbird_open,
    .release = chatbird_release,
    .unlocked_ioctl = chatbird_unlocked_ioctl};

static struct usb_class_driver chatbird_class = {
    .name = "cb%d",
    .fops = &chatbird_fops,
    .minor_base = CHATBIRD_MINOR_BASE,
};

static int chatbird_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_chatbird *dev = NULL; // chatbird driver instance
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;

	int i;
	int retval = -ENODEV;

	printk(KERN_INFO "Chatbird (%04X:%04X) plugged\n", id->idVendor,
	       id->idProduct);
	if (!udev)
	{
		DBG_ERR("udev is NULL");
		goto exit;
	}

	dev = kzalloc(sizeof(struct usb_chatbird), GFP_KERNEL);
	if (!dev)
	{
		DBG_ERR("cannot allocate memory for struct usb_chatbird");
		retval = -ENOMEM;
		goto exit;
	}

	sema_init(&dev->sem, 1);
	spin_lock_init(&dev->cmd_spinlock);

	dev->udev = udev;
	dev->interface = interface;
	iface_desc = interface->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++)
	{
		endpoint = &iface_desc->endpoint[i].desc;

		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT))
		{
			dev->int_in_endpoint = endpoint;
			DBG_INFO("IN endpoint found");
		}
		else if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT))
		{
			dev->int_out_endpoint = endpoint;
			DBG_INFO("OUT endpoint found");
		}
	}
	if (!dev->int_in_endpoint)
	{
		DBG_ERR("could not find interrupt in endpoint");
		goto error;
	}
	if (!dev->int_out_endpoint)
	{
		DBG_ERR("could not find interrupt out endpoint");
		goto error;
	}

	dev->int_in_buffer = kmalloc(dev->int_in_endpoint->wMaxPacketSize, GFP_KERNEL);
	if (!dev->int_in_buffer)
	{
		DBG_ERR("could not allocate int_in_buffer");
		retval = -ENOMEM;
		goto error;
	}
	dev->int_out_buffer = kmalloc(CHATBIRD_INT_OUT_BUFFER_SIZE, GFP_KERNEL);
	if (!dev->int_out_buffer)
	{
		DBG_ERR("could not allocate int_out_buffer");
		retval = -ENOMEM;
		goto error;
	}

	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->int_in_urb)
	{
		DBG_ERR("could not allocate int_in_urb");
		retval = -ENOMEM;
		goto error;
	}
	dev->int_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->int_out_urb)
	{
		DBG_ERR("could not allocate int_out_urb");
		retval = -ENOMEM;
		goto error;
	}

	dev->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->ctrl_urb)
	{
		DBG_ERR("could not allocate ctrl_urb");
		retval = -ENOMEM;
		goto error;
	}
	dev->ctrl_buffer = kzalloc(CHATBIRD_CTRL_BUFFER_SIZE, GFP_KERNEL);
	if (!dev->ctrl_buffer)
	{
		DBG_ERR("could not allocate ctrl_buffer");
		retval = -ENOMEM;
		goto error;
	}
	dev->ctrl_dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!dev->ctrl_dr)
	{
		DBG_ERR("could not allocate usb_ctrlrequest");
		retval = -ENOMEM;
		goto error;
	}

	dev->ctrl_dr->bRequestType = CHATBIRD_CTRL_REQUEST_TYPE;
	dev->ctrl_dr->bRequest = CHATBIRD_CTRL_REQUEST;
	dev->ctrl_dr->wValue = cpu_to_le16(CHATBIRD_CTRL_VALUE);
	dev->ctrl_dr->wIndex = cpu_to_le16(CHATBIRD_CTRL_INDEX);
	dev->ctrl_dr->wLength = cpu_to_le16(CHATBIRD_CTRL_BUFFER_SIZE);

	usb_fill_control_urb(dev->ctrl_urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (unsigned char *)dev->ctrl_dr,
			     dev->ctrl_buffer,
			     CHATBIRD_CTRL_BUFFER_SIZE,
			     chatbird_ctrl_callback,
			     dev);

	// retval = usb_control_msg(
	//     dev->udev,
	//     usb_sndctrlpipe(dev->udev, 0),
	//     0,
	//     0,
	//     0x1234,
	//     0,
	//     0,
	//     0,
	//     500
	// );
	// DBG_INFO("control_msg: %d", retval);

	/* Retrieve a serial. */
	if (!usb_string(udev, udev->descriptor.iSerialNumber,
			dev->serial_number, sizeof(dev->serial_number)))
	{
		DBG_ERR("could not retrieve serial number");
		goto error;
	}
	/* Save our data pointer in this interface device. */
	usb_set_intfdata(interface, dev);

	/* We can register the device now, as it is ready. */
	retval = usb_register_dev(interface, &chatbird_class);
	if (retval)
	{
		DBG_ERR("not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	dev->minor = interface->minor;

	DBG_INFO("USB chatbird now attached to /dev/cb%d",
		 interface->minor - CHATBIRD_MINOR_BASE);
exit:
	return retval;

error:
	// TODO:
	chatbird_delete(dev);
	return retval;
}

static void chatbird_disconnect(struct usb_interface *interface)
{
	struct usb_chatbird *dev;
	int minor;

	mutex_lock(&disconnect_mutex); /* Not interruptible */

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	down(&dev->sem); /* Not interruptible */

	minor = dev->minor;

	/* Give back our minor. */
	usb_deregister_dev(interface, &chatbird_class);

	/* If the device is not opened, then we clean up right now. */
	if (!dev->open_count)
	{
		up(&dev->sem);
		chatbird_delete(dev);
	}
	else
	{
		dev->udev = NULL;
		up(&dev->sem);
	}

	mutex_unlock(&disconnect_mutex);

	DBG_INFO("USB chatbird /dev/cb%d now disconnected",
		 minor - CHATBIRD_MINOR_BASE);
}

static struct usb_driver chatbird_driver =
    {
	.name = "chatbird_driver",
	.id_table = chatbird_table,
	.probe = chatbird_probe,
	.disconnect = chatbird_disconnect,
};

static int __init chatbird_init(void)
{
	int result;

	DBG_INFO("Registering chatbird driver...");
	result = usb_register(&chatbird_driver);
	if (result)
	{
		DBG_ERR("Registering driver failed");
	}
	else
	{
		DBG_INFO("Driver succesfully registered");
	}
	return result;
}

static void __exit chatbird_exit(void)
{
	usb_deregister(&chatbird_driver);
	DBG_INFO("Driver deregistered");
}

module_init(chatbird_init);
module_exit(chatbird_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter-Simon Dieterich <dieterich.peter@gmail.com>");
MODULE_DESCRIPTION("Chatbird USB Driver");