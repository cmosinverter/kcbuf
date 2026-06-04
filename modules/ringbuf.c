#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

MODULE_DESCRIPTION("A ring buffer char device");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "ringbuf"
#define RING_SIZE 4096
#define MASK (RING_SIZE - 1)

static DEFINE_MUTEX(mymutex);

static char ring_buf[RING_SIZE];
static unsigned int head;  /* next byte to read */
static unsigned int tail;  /* next byte to write */

static ssize_t ring_read(struct file *file, char __user *ubuf, size_t len,
                         loff_t *ppos)
{
    size_t chunk;

    if (mutex_lock_interruptible(&mymutex))
        return -ERESTARTSYS;

    chunk = min(len, (size_t)(tail - head));
    if (chunk > RING_SIZE - (head & MASK))
        chunk = RING_SIZE - (head & MASK);

    if (copy_to_user(ubuf, ring_buf + (head & MASK), chunk)) {
        mutex_unlock(&mymutex);
        return -EFAULT;
    }
    head += chunk;
    mutex_unlock(&mymutex);

    return chunk;
}

static ssize_t ring_write(struct file *file, const char __user *ubuf,
                          size_t len, loff_t *ppos)
{
    size_t chunk;

    if (mutex_lock_interruptible(&mymutex))
        return -ERESTARTSYS;

    chunk = min(len, (size_t)(RING_SIZE - (tail - head)));
    if (chunk > RING_SIZE - (tail & MASK))
        chunk = RING_SIZE - (tail & MASK);

    if (copy_from_user(ring_buf + (tail & MASK), ubuf, chunk)) {
        mutex_unlock(&mymutex);
        return -EFAULT;
    }
    tail += chunk;
    mutex_unlock(&mymutex);

    return chunk;
}

static const struct file_operations ring_fops = {
    .owner = THIS_MODULE,
    .read = ring_read,
    .write = ring_write,
};

static struct miscdevice ring_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &ring_fops,
    .mode = 0666,
};

static int __init ringbuf_init(void)
{
    int ret = misc_register(&ring_misc);
    if (ret)
        return ret;
    pr_info("ringbuf: /dev/%s registered\n", DEVICE_NAME);
    return 0;
}

static void __exit ringbuf_exit(void)
{
    misc_deregister(&ring_misc);
    pr_info("ringbuf: unregistered\n");
}

module_init(ringbuf_init);
module_exit(ringbuf_exit);
