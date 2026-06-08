#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

MODULE_DESCRIPTION("A ring buffer char device");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "ringbuf"
#define RING_SIZE 4096
#define MASK (RING_SIZE - 1)

static char ring_buf[RING_SIZE];
static unsigned int head;  /* next byte to read */
static unsigned int tail;  /* next byte to write */

static ssize_t ring_read(struct file *file, char __user *ubuf, size_t len,
                         loff_t *ppos)
{
    unsigned int t = smp_load_acquire(&tail);
    unsigned int h = head;
    size_t chunk = min(len, (size_t)(t - h));

    if (chunk > RING_SIZE - (h & MASK))
        chunk = RING_SIZE - (h & MASK);

    if (copy_to_user(ubuf, ring_buf + (h & MASK), chunk)) {
        return -EFAULT;
    }

    smp_store_release(&head, h + chunk);

    return chunk;
}

static ssize_t ring_write(struct file *file, const char __user *ubuf,
                          size_t len, loff_t *ppos)
{

    unsigned int t = tail;
    unsigned int h = smp_load_acquire(&head);
    size_t chunk = min(len, (size_t)(RING_SIZE - (t - h)));

    if (chunk > RING_SIZE - (t & MASK))
        chunk = RING_SIZE - (t & MASK);

    if (copy_from_user(ring_buf + (t & MASK), ubuf, chunk)) {
        return -EFAULT;
    }

    smp_store_release(&tail, t + chunk);

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
