/**************************************************************
* Class: CSC-415-03 Fall 2024
* Name: Atharva Walawalkar
* Student ID: 924254653
* GitHub Name: Atharvawal2002
* Project: Assignment 6 - Device Driver
*
* File: rle_driver.c
*
* Description: RLE (Run Length Encoding) Device Driver
*
**************************************************************/


#include <linux/init.h>      // Module init/exit macros
#include <linux/module.h>    // Core module functionality
#include <linux/kernel.h>    // Kernel types and printk
#include <linux/fs.h>        // File operations support
#include <linux/uaccess.h>   // copy_to/from_user functions
#include <linux/device.h>    // Device class and management
#include <linux/cdev.h>      // Character device support
#include <linux/slab.h>      // kmalloc/kfree memory management

/* Device specific definitions */
#define DEVICE_NAME "rledev"
#define CLASS_NAME "rle"
#define MAX_BUFFER_SIZE 4096

/*
 * IOCTL command definitions
 *
 * _IOW macro constructs command numbers using:
 * - First Argument ('r'): Unique magic number for this driver
 * - Second Argument (1): Command sequence number
 * - Third Argument (int): Type of data being passed
 * 
 * _IOW indicates data transfer direction (Write from user to kernel)
 */
#define RLE_IOC_MAGIC 'r'
#define RLE_SET_MODE _IOW(RLE_IOC_MAGIC, 1, int)

/* Operation modes for the driver */
enum rle_mode 
{
    RLE_MODE_COMPRESS = 0,
    RLE_MODE_DECOMPRESS = 1
};

/**
 * Private data structure for RLE device
 *
 * Design decisions:
 * 1. Dynamic buffer allocation:
 *    - Allows efficient memory use
 *    - Supports variable input sizes
 *    - Enables per-instance memory management
 *
 * 2. Mode flag in structure:
 *    - Maintains state per file descriptor
 *    - Allows concurrent operations with different modes
 *    - Simplifies IOCTL handling
 *
 * 3. Buffer size tracking:
 *    - Prevents buffer overflow
 *    - Enables accurate data length management
 *    - Improves error detection
 */

struct rle_dev
{
    char *buffer;              /* Data buffer */
    size_t buffer_size;        /* Current size of data in buffer */
    enum rle_mode mode;        /* Current operation mode */
};

/* Static variables for device management */
static int major_number;
static struct class* rle_class = NULL;
static struct device* rle_device = NULL;

/* Function prototypes */
static int __init rle_init(void);
static void __exit rle_exit(void);
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);
static long dev_ioctl(struct file *, unsigned int, unsigned long);

/* File operations structure */
static struct file_operations fops = 
{
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
    .unlocked_ioctl = dev_ioctl
};

/**
 * Compresses data using RLE algorithm
 * @input: Input data to compress
 * @input_len: Length of input data
 * @output: Buffer to store compressed data
 * @return: Length of compressed data
 */
static size_t compress_rle(const char *input, size_t input_len, char *output) 
{
    size_t out_pos = 0;
    size_t i = 0;
    char curr_char;
    size_t run_length;

    printk(KERN_DEBUG "RLE driver: Starting compression of %zu bytes\n", input_len);

    while (i < input_len) 
    {
        curr_char = input[i];
        run_length = 1;

        while ((i + run_length < input_len) && 
               (input[i + run_length] == curr_char) && 
               (run_length < 255)) 
            {
            run_length++;
            }

        output[out_pos++] = (char)run_length;
        output[out_pos++] = curr_char;
        i += run_length;
    }

    printk(KERN_DEBUG "RLE driver: Compression complete, produced %zu bytes\n", out_pos);
    return out_pos;
}

/**
 * Decompresses RLE encoded data
 * @input: Compressed input data
 * @input_len: Length of compressed data
 * @output: Buffer to store decompressed data
 * @return: Length of decompressed data
 */
static size_t decompress_rle(const char *input, size_t input_len, char *output) 
{
    size_t out_pos = 0;
    size_t i = 0;
    unsigned char count;
    char value;

    printk(KERN_DEBUG "RLE driver: Starting decompression of %zu bytes\n", input_len);

    while (i + 1 < input_len) 
    {
        count = (unsigned char)input[i++];
        value = input[i++];

        while (count-- > 0 && out_pos < MAX_BUFFER_SIZE) 
        {
            output[out_pos++] = value;
        }
    }

    printk(KERN_DEBUG "RLE driver: Decompression complete, produced %zu bytes\n", out_pos);
    return out_pos;
}

/**
 * Device open function
 * Allocates and initializes private data structure for this instance
 */
static int dev_open(struct inode *inodep, struct file *filep) 
{
    struct rle_dev *dev;

    dev = kmalloc(sizeof(struct rle_dev), GFP_KERNEL);
    if (!dev) 
    {
        printk(KERN_ERR "RLE driver: Failed to allocate private data\n");
        return -ENOMEM;
    }

    dev->buffer = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if (!dev->buffer) 
    {
        printk(KERN_ERR "RLE driver: Failed to allocate buffer\n");
        kfree(dev);
        return -ENOMEM;
    }

    memset(dev->buffer, 0, MAX_BUFFER_SIZE);
    dev->buffer_size = 0;
    dev->mode = RLE_MODE_COMPRESS;
    filep->private_data = dev;

    printk(KERN_INFO "RLE driver: Device opened\n");
    return 0;
}

/**
 * Device release function
 * Cleans up private data structure
 */
static int dev_release(struct inode *inodep, struct file *filep) {
    struct rle_dev *dev = filep->private_data;
    
    if (dev) 
    {
        if (dev->buffer) 
        {
            kfree(dev->buffer);
        }
        kfree(dev);
        filep->private_data = NULL;
    }
    
    printk(KERN_INFO "RLE driver: Device closed\n");
    return 0;
}

/**
 * Write operation for RLE device
 *
 * Implementation considerations:
 * 1. Clears buffer before new write:
 * 2. Size validation:
 * 3. Copy-from-user used because:
 */
static ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    struct rle_dev *dev = filep->private_data;
    int ret;

    printk(KERN_DEBUG "RLE Write: Starting with len=%zu\n", len);

    if (!dev || !dev->buffer) {
        printk(KERN_ERR "RLE Write: Invalid device state\n");
        return -EINVAL;
    }

    if (len > MAX_BUFFER_SIZE) {
        printk(KERN_WARNING "RLE Write: Input too large (max=%d)\n", MAX_BUFFER_SIZE);
        return -EINVAL;
    }

    // Clear buffer before writing
    memset(dev->buffer, 0, MAX_BUFFER_SIZE);
    
    ret = copy_from_user(dev->buffer, buffer, len);
    if (ret) {
        printk(KERN_ERR "RLE Write: copy_from_user failed, ret=%d\n", ret);
        return -EFAULT;
    }

    dev->buffer_size = len;
    
    // Debug print first few bytes
    printk(KERN_DEBUG "RLE Write: First 4 bytes: %02x %02x %02x %02x\n",
           dev->buffer[0], dev->buffer[1], 
           len > 2 ? dev->buffer[2] : 0, 
           len > 3 ? dev->buffer[3] : 0);
    
    printk(KERN_DEBUG "RLE Write: Stored %zu bytes successfully\n", dev->buffer_size);
    return len;
}
/**
 * Device read function
 * Processes data according to current mode and copies to user space
 */
static ssize_t dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    struct rle_dev *dev = filep->private_data;
    char *temp_buffer;
    size_t result_size;
    int ret;

    printk(KERN_DEBUG "RLE Read: Starting with mode=%d, buffer_size=%zu\n", 
           dev->mode, dev->buffer_size);

    if (!dev || !dev->buffer) {
        printk(KERN_ERR "RLE Read: Invalid device state\n");
        return -EINVAL;
    }

    temp_buffer = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if (!temp_buffer) {
        printk(KERN_ERR "RLE Read: Failed to allocate temp buffer\n");
        return -ENOMEM;
    }

    memset(temp_buffer, 0, MAX_BUFFER_SIZE);

    // Debug print first few bytes of input
    printk(KERN_DEBUG "RLE Read: Input first 4 bytes: %02x %02x %02x %02x\n",
           dev->buffer[0], dev->buffer[1],
           dev->buffer_size > 2 ? dev->buffer[2] : 0,
           dev->buffer_size > 3 ? dev->buffer[3] : 0);

    if (dev->mode == RLE_MODE_COMPRESS) {
        printk(KERN_DEBUG "RLE Read: Starting compression\n");
        result_size = compress_rle(dev->buffer, dev->buffer_size, temp_buffer);
        printk(KERN_DEBUG "RLE Read: Compression produced %zu bytes\n", result_size);
    } else {
        printk(KERN_DEBUG "RLE Read: Starting decompression\n");
        result_size = decompress_rle(dev->buffer, dev->buffer_size, temp_buffer);
        printk(KERN_DEBUG "RLE Read: Decompression produced %zu bytes\n", result_size);
    }

    if (result_size > len) {
        printk(KERN_ERR "RLE Read: Result too large for buffer\n");
        kfree(temp_buffer);
        return -EINVAL;
    }

    ret = copy_to_user(buffer, temp_buffer, result_size);
    kfree(temp_buffer);

    if (ret) {
        return -EFAULT;
    }

    printk(KERN_DEBUG "RLE Read: Successfully sent %zu bytes\n", result_size);
    return result_size;
}
/**
 * Device ioctl function
 * Handles device control commands
 * Currently supports setting compression/decompression mode
 */
static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) 
{
    struct rle_dev *dev = filep->private_data;
    
    if (!dev) 
    {
        return -EINVAL;
    }

    switch (cmd) 
    {
        case RLE_SET_MODE:
            if (arg != RLE_MODE_COMPRESS && arg != RLE_MODE_DECOMPRESS) 
            {
                printk(KERN_ERR "RLE driver: Invalid mode\n");
                return -EINVAL;
            }
            
            dev->mode = arg;
            printk(KERN_INFO "RLE driver: Mode set to %s\n", 
                   dev->mode == RLE_MODE_COMPRESS ? "compress" : "decompress");
            return 0;
            
        default:
            return -ENOTTY;
    }
}

/**
 * Module initialization function
 * Sets up character device, class, and device node
 */
static int __init rle_init(void) 
{
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) 
    {
        printk(KERN_ALERT "RLE driver: Failed to register a major number\n");
        return major_number;
    }

    rle_class = class_create(CLASS_NAME);
    if (IS_ERR(rle_class)) 
    {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "RLE driver: Failed to register device class\n");
        return PTR_ERR(rle_class);
    }

    rle_device = device_create(rle_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(rle_device)) 
    {
        class_destroy(rle_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "RLE driver: Failed to create the device\n");
        return PTR_ERR(rle_device);
    }

    printk(KERN_INFO "RLE driver: Device created successfully\n");
    return 0;
}

/**
 * Module cleanup function
 * Removes device node, class, and character device
 */
static void __exit rle_exit(void) 
{
    device_destroy(rle_class, MKDEV(major_number, 0));
    class_unregister(rle_class);
    class_destroy(rle_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "RLE driver: Unloaded successfully\n");
}

module_init(rle_init);
module_exit(rle_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Atharva Walawalkar");
MODULE_DESCRIPTION("A kernel space RLE compression/decompression driver");
MODULE_VERSION("0.1");

