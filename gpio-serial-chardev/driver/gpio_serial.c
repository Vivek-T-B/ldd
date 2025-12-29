/*
 * GPIO Serial Communication Character Device Driver
 * Main implementation with blocking I/O, synchronization, and sysfs interface
 *
 * Features:
 * - Multiple device instances (up to 3 devices)
 * - Dynamic character device registration
 * - Blocking I/O with wait queues
 * - Spinlock and mutex synchronization
 * - GPIO bit-banging protocol
 * - Interrupt-driven and polled modes
 * - Comprehensive sysfs interface
 */

#include "gpio_serial.h"

/* Global variables */
static struct gpio_serial_dev *gpio_serial_devices[GPIO_SERIAL_NUM_DEVICES];
static struct class *gpio_serial_class;
static dev_t gpio_serial_devt;
static dev_t gpio_serial_first_dev;
static int gpio_serial_major;

/* Module parameters */
int debug_level = 1;  /* Default to normal debug level */
module_param(debug_level, int, 0644);

/* Default device configurations */
static struct {
    int tx_gpio;
    int rx_gpio;
    const char *name;
} gpio_serial_defaults[GPIO_SERIAL_NUM_DEVICES] = {
    {18, 19, "gpio_serial0"},  /* Default GPIO pins for device 0 */
    {20, 21, "gpio_serial1"},  /* Default GPIO pins for device 1 */
    {22, 23, "gpio_serial2"}   /* Default GPIO pins for device 2 */
};

/*
 * Character Device File Operations
 */

/**
 * gpio_serial_open - Open character device
 * @inode: Inode structure
 * @filp: File structure
 *
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_open(struct inode *inode, struct file *filp)
{
    struct gpio_serial_dev *dev;
    int ret = 0;

    GPIO_SERIAL_DEBUG("Opening device\n");

    /* Get device structure from inode */
    dev = container_of(inode->i_cdev, struct gpio_serial_dev, cdev);
    if (!dev) {
        GPIO_SERIAL_ERROR("Failed to get device structure\n");
        return -ENODEV;
    }

    /* Increment device usage count */
    if (mutex_lock_interruptible(&dev->mutex)) {
        GPIO_SERIAL_DEBUG("Open interrupted by signal\n");
        return -ERESTARTSYS;
    }

    /* Initialize file private data */
    filp->private_data = dev;

    /* Check if device is available */
    if (!dev->tx_gpio || !dev->rx_gpio) {
        GPIO_SERIAL_ERROR("Device %s not properly configured\n", dev->name);
        ret = -EINVAL;
        goto unlock;
    }

    GPIO_SERIAL_INFO("Device %s opened successfully\n", dev->name);

unlock:
    mutex_unlock(&dev->mutex);
    return ret;
}

/**
 * gpio_serial_release - Release character device
 * @inode: Inode structure
 * @filp: File structure
 *
 * Returns: 0 on success
 */
int gpio_serial_release(struct inode *inode, struct file *filp)
{
    struct gpio_serial_dev *dev = GPIO_SERIAL_GET_DEV(filp);

    GPIO_SERIAL_DEBUG("Releasing device %s\n", dev->name);

    /* Clean up any pending work */
    cancel_work_sync(&dev->work);

    GPIO_SERIAL_INFO("Device %s released\n", dev->name);

    return 0;
}

/**
 * gpio_serial_read - Read from device
 * @filp: File structure
 * @buf: User buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns: Number of bytes read, negative error code on failure
 */
ssize_t gpio_serial_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct gpio_serial_dev *dev = GPIO_SERIAL_GET_DEV(filp);
    ssize_t ret = 0;
    unsigned long flags;
    int available;

    GPIO_SERIAL_DEBUG("Read request: %zu bytes\n", count);

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    /* Check if blocking mode is enabled */
    if (dev->blocking_mode) {
        GPIO_SERIAL_DEBUG("Blocking read mode enabled\n");

        /* Wait for data to be available */
        ret = wait_event_interruptible_timeout(
            dev->read_wait,
            dev->data_available || (dev->rx_head != dev->rx_tail),
            dev->timeout
        );

        if (ret == 0) {
            GPIO_SERIAL_DEBUG("Read timeout\n");
            ret = -ETIMEDOUT;
            goto unlock;
        }

        if (ret < 0) {
            GPIO_SERIAL_DEBUG("Read interrupted by signal\n");
            goto unlock;
        }
    }

    /* Calculate available data */
    spin_lock_irqsave(&dev->spinlock, flags);
    available = (dev->rx_head - dev->rx_tail + sizeof(dev->rx_buffer)) % sizeof(dev->rx_buffer);
    spin_unlock_irqrestore(&dev->spinlock, flags);

    if (available == 0) {
        GPIO_SERIAL_DEBUG("No data available\n");
        ret = dev->blocking_mode ? 0 : -EAGAIN;
        goto unlock;
    }

    /* Limit read size to available data */
    count = GPIO_SERIAL_MIN(count, (size_t)available);

    /* Copy data from RX buffer to user space */
    ret = copy_to_user(buf, &dev->rx_buffer[dev->rx_tail], count);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to copy %zd bytes to user space\n", ret);
        ret = -EFAULT;
        goto unlock;
    }

    /* Update RX buffer tail */
    spin_lock_irqsave(&dev->spinlock, flags);
    dev->rx_tail = (dev->rx_tail + count) % sizeof(dev->rx_buffer);
    
    /* Update data availability flag */
    if (dev->rx_head == dev->rx_tail) {
        dev->data_available = false;
    }
    
    dev->bytes_received += count;
    spin_unlock_irqrestore(&dev->spinlock, flags);

    GPIO_SERIAL_DEBUG("Read %zu bytes successfully\n", count);

unlock:
    mutex_unlock(&dev->mutex);
    return ret;
}

/**
 * gpio_serial_write - Write to device
 * @filp: File structure
 * @buf: User buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns: Number of bytes written, negative error code on failure
 */
ssize_t gpio_serial_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct gpio_serial_dev *dev = GPIO_SERIAL_GET_DEV(filp);
    ssize_t ret = 0;
    unsigned char *kernel_buf;
    size_t i;

    GPIO_SERIAL_DEBUG("Write request: %zu bytes\n", count);

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    /* Allocate kernel buffer */
    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (!kernel_buf) {
        GPIO_SERIAL_ERROR("Failed to allocate kernel buffer\n");
        ret = -ENOMEM;
        goto unlock;
    }

    /* Copy data from user space */
    ret = copy_from_user(kernel_buf, buf, count);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to copy %zd bytes from user space\n", ret);
        ret = -EFAULT;
        goto free_buf;
    }

    /* Send each byte using GPIO bit-banging */
    for (i = 0; i < count; i++) {
        gpio_serial_send_byte(dev, kernel_buf[i]);
    }

    dev->bytes_sent += count;
    GPIO_SERIAL_DEBUG("Wrote %zu bytes successfully\n", count);
    ret = count;

free_buf:
    kfree(kernel_buf);
unlock:
    mutex_unlock(&dev->mutex);
    return ret;
}

/**
 * gpio_serial_ioctl - Device control
 * @filp: File structure
 * @cmd: IOCTL command
 * @arg: Argument pointer
 *
 * Returns: 0 on success, negative error code on failure
 */
long gpio_serial_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct gpio_serial_dev *dev = GPIO_SERIAL_GET_DEV(filp);
    int ret = 0;

    GPIO_SERIAL_DEBUG("IOCTL command: 0x%08x\n", cmd);

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    switch (cmd) {
    case GPIO_SERIAL_RESET:
        gpio_serial_protocol_reset(dev);
        GPIO_SERIAL_INFO("Device %s reset via IOCTL\n", dev->name);
        break;

    case GPIO_SERIAL_GET_STATS:
        /* Copy statistics to user space */
        if (copy_to_user((void __user *)arg, &dev->bytes_received, sizeof(dev->bytes_received)) ||
            copy_to_user((void __user *)(arg + sizeof(dev->bytes_received)), &dev->bytes_sent, sizeof(dev->bytes_sent)) ||
            copy_to_user((void __user *)(arg + 2 * sizeof(dev->bytes_sent)), &dev->errors, sizeof(dev->errors))) {
            ret = -EFAULT;
        }
        break;

    case GPIO_SERIAL_CLEAR_STATS:
        dev->bytes_received = 0;
        dev->bytes_sent = 0;
        dev->errors = 0;
        dev->interrupts = 0;
        GPIO_SERIAL_INFO("Device %s statistics cleared\n", dev->name);
        break;

    default:
        GPIO_SERIAL_ERROR("Unknown IOCTL command: 0x%08x\n", cmd);
        ret = -EINVAL;
        break;
    }

    mutex_unlock(&dev->mutex);
    return ret;
}

/* File operations structure */
const struct file_operations gpio_serial_fops = {
    .owner = THIS_MODULE,
    .open = gpio_serial_open,
    .release = gpio_serial_release,
    .read = gpio_serial_read,
    .write = gpio_serial_write,
    .unlocked_ioctl = gpio_serial_ioctl,
};

/*
 * GPIO and Protocol Functions
 */

/**
 * gpio_serial_setup_gpios - Configure GPIO pins
 * @dev: Device structure
 *
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_setup_gpios(struct gpio_serial_dev *dev)
{
    int ret = 0;

    GPIO_SERIAL_DEBUG("Setting up GPIO pins for device %s\n", dev->name);

    /* Configure TX GPIO */
    if (!gpio_is_valid(dev->tx_gpio)) {
        GPIO_SERIAL_ERROR("Invalid TX GPIO: %d\n", dev->tx_gpio);
        return -EINVAL;
    }

    ret = gpio_request(dev->tx_gpio, dev->name);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to request TX GPIO %d: %d\n", dev->tx_gpio, ret);
        return ret;
    }

    gpio_direction_output(dev->tx_gpio, GPIO_SERIAL_STOP_BIT);
    gpio_serial_write_bit(dev, GPIO_SERIAL_STOP_BIT);

    /* Configure RX GPIO */
    if (!gpio_is_valid(dev->rx_gpio)) {
        GPIO_SERIAL_ERROR("Invalid RX GPIO: %d\n", dev->rx_gpio);
        gpio_free(dev->tx_gpio);
        return -EINVAL;
    }

    ret = gpio_request(dev->rx_gpio, dev->name);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to request RX GPIO %d: %d\n", dev->rx_gpio, ret);
        gpio_free(dev->tx_gpio);
        return ret;
    }

    gpio_direction_input(dev->rx_gpio);

    /* Request IRQ for RX GPIO if in interrupt mode */
    if (dev->irq_number > 0) {
        ret = request_irq(dev->irq_number, gpio_serial_irq_handler,
                         IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                         dev->name, dev);
        if (ret) {
            GPIO_SERIAL_ERROR("Failed to request IRQ %d: %d\n", dev->irq_number, ret);
            gpio_free(dev->tx_gpio);
            gpio_free(dev->rx_gpio);
            return ret;
        }
        GPIO_SERIAL_INFO("IRQ %d requested for device %s\n", dev->irq_number, dev->name);
    }

    return 0;
}

/**
 * gpio_serial_cleanup_gpios - Release GPIO resources
 * @dev: Device structure
 */
void gpio_serial_cleanup_gpios(struct gpio_serial_dev *dev)
{
    GPIO_SERIAL_DEBUG("Cleaning up GPIO pins for device %s\n", dev->name);

    if (dev->irq_number > 0) {
        free_irq(dev->irq_number, dev);
    }

    if (gpio_is_valid(dev->tx_gpio)) {
        gpio_free(dev->tx_gpio);
    }

    if (gpio_is_valid(dev->rx_gpio)) {
        gpio_free(dev->rx_gpio);
    }
}

/**
 * gpio_serial_write_bit - Write single bit to GPIO
 * @dev: Device structure
 * @bit: Bit value (0 or 1)
 */
void gpio_serial_write_bit(struct gpio_serial_dev *dev, int bit)
{
    gpio_set_value(dev->tx_gpio, bit);
    udelay(GPIO_SERIAL_BAUDRATE_DELAY);
}

/**
 * gpio_serial_read_bit - Read single bit from GPIO
 * @dev: Device structure
 *
 * Returns: Bit value (0 or 1)
 */
int gpio_serial_read_bit(struct gpio_serial_dev *dev)
{
    return gpio_get_value(dev->rx_gpio);
}

/**
 * gpio_serial_send_byte - Send byte using bit-banging protocol
 * @dev: Device structure
 * @byte: Byte to send
 */
void gpio_serial_send_byte(struct gpio_serial_dev *dev, unsigned char byte)
{
    int i;

    GPIO_SERIAL_DEBUG("Sending byte: 0x%02x\n", byte);

    /* Send start bit */
    gpio_serial_write_bit(dev, GPIO_SERIAL_START_BIT);

    /* Send data bits (LSB first) */
    for (i = 0; i < GPIO_SERIAL_BITS_PER_BYTE; i++) {
        gpio_serial_write_bit(dev, (byte >> i) & 0x01);
    }

    /* Send stop bit */
    gpio_serial_write_bit(dev, GPIO_SERIAL_STOP_BIT);

    /* Additional stop bit for timing */
    gpio_serial_write_bit(dev, GPIO_SERIAL_STOP_BIT);
}

/**
 * gpio_serial_protocol_init - Initialize protocol state machine
 * @dev: Device structure
 */
void gpio_serial_protocol_init(struct gpio_serial_dev *dev)
{
    dev->rx_head = 0;
    dev->rx_tail = 0;
    dev->data_available = false;
    dev->current_byte = 0;
    dev->bit_position = 0;
    dev->in_frame = false;
    dev->last_bit_time = jiffies;
}

/**
 * gpio_serial_protocol_reset - Reset protocol state machine
 * @dev: Device structure
 */
void gpio_serial_protocol_reset(struct gpio_serial_dev *dev)
{
    unsigned long flags;

    GPIO_SERIAL_DEBUG("Resetting protocol state for device %s\n", dev->name);

    spin_lock_irqsave(&dev->spinlock, flags);
    gpio_serial_protocol_init(dev);
    spin_unlock_irqrestore(&dev->spinlock, flags);
}

/*
 * Interrupt Handler
 */

/**
 * gpio_serial_irq_handler - Interrupt handler for RX GPIO
 * @irq: IRQ number
 * @dev_id: Device ID
 *
 * Returns: IRQ_HANDLED if interrupt was handled
 */
irqreturn_t gpio_serial_irq_handler(int irq, void *dev_id)
{
    struct gpio_serial_dev *dev = dev_id;
    unsigned long flags;
    int bit_value;
    unsigned long current_time = jiffies;

    GPIO_SERIAL_DEBUG("IRQ %d triggered for device %s\n", irq, dev->name);

    spin_lock_irqsave(&dev->spinlock, flags);

    dev->interrupts++;

    /* Check if we're in the middle of receiving a frame */
    if (!dev->in_frame) {
        /* Check for start bit */
        bit_value = gpio_serial_read_bit(dev);
        if (bit_value == GPIO_SERIAL_START_BIT) {
            dev->in_frame = true;
            dev->current_byte = 0;
            dev->bit_position = 0;
            dev->last_bit_time = current_time;
        }
    } else {
        /* Check if enough time has passed for the next bit */
        if (time_after(current_time, dev->last_bit_time + 
                      usecs_to_jiffies(GPIO_SERIAL_BAUDRATE_DELAY))) {
            
            /* Read current bit */
            bit_value = gpio_serial_read_bit(dev);

            /* Store bit in current byte */
            if (dev->bit_position < GPIO_SERIAL_BITS_PER_BYTE) {
                if (bit_value) {
                    dev->current_byte |= (1 << dev->bit_position);
                }
                dev->bit_position++;
                dev->last_bit_time = current_time;
            } else {
                /* Check for stop bit */
                if (bit_value == GPIO_SERIAL_STOP_BIT) {
                    /* Complete byte received, add to buffer */
                    dev->rx_buffer[dev->rx_head] = dev->current_byte;
                    dev->rx_head = (dev->rx_head + 1) % sizeof(dev->rx_buffer);
                    dev->data_available = true;

                    /* Wake up waiting processes */
                    wake_up_interruptible(&dev->read_wait);

                    /* Echo mode - send back the received byte */
                    if (dev->echo_mode) {
                        gpio_serial_send_byte(dev, dev->current_byte);
                    }

                    GPIO_SERIAL_DEBUG("Received byte: 0x%02x\n", dev->current_byte);
                }

                /* Reset frame state */
                dev->in_frame = false;
                dev->bit_position = 0;
            }
        }
    }

    spin_unlock_irqrestore(&dev->spinlock, flags);

    return IRQ_HANDLED;
}

/*
 * Work Queue Handler
 */

/**
 * gpio_serial_work_handler - Work queue handler for deferred processing
 * @work: Work structure
 */
void gpio_serial_work_handler(struct work_struct *work)
{
    struct gpio_serial_dev *dev = container_of(work, struct gpio_serial_dev, work);
    
    GPIO_SERIAL_DEBUG("Processing deferred work for device %s\n", dev->name);
    
    /* Add any deferred processing here */
    /* This could include statistics updates, cleanup, etc. */
}

/*
 * Device Management Functions
 */

/**
 * gpio_serial_setup_device - Setup individual device
 * @dev: Device structure
 * @device_id: Device instance ID
 *
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_setup_device(struct gpio_serial_dev *dev, int device_id)
{
    int ret = 0;

    memset(dev, 0, sizeof(struct gpio_serial_dev));

    /* Initialize device structure */
    dev->device_id = device_id;
    snprintf(dev->name, sizeof(dev->name), "%s%d", GPIO_SERIAL_NAME, device_id);
    dev->tx_gpio = gpio_serial_defaults[device_id].tx_gpio;
    dev->rx_gpio = gpio_serial_defaults[device_id].rx_gpio;
    dev->blocking_mode = true;  /* Default to blocking mode */
    dev->timeout = 5 * HZ;      /* 5 second default timeout */
    dev->echo_mode = false;     /* Default to no echo */

    /* Initialize synchronization primitives */
    spin_lock_init(&dev->spinlock);
    mutex_init(&dev->mutex);
    init_waitqueue_head(&dev->read_wait);

    /* Initialize protocol state */
    gpio_serial_protocol_init(dev);

    /* Initialize work queue */
    INIT_WORK(&dev->work, gpio_serial_work_handler);

    /* Allocate device number */
    dev->devt = MKDEV(gpio_serial_major, device_id);

    /* Initialize character device */
    cdev_init(&dev->cdev, &gpio_serial_fops);
    dev->cdev.owner = THIS_MODULE;

    /* Add character device */
    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to add character device: %d\n", ret);
        return ret;
    }

    /* Create device in sysfs */
    dev->device = device_create(gpio_serial_class, NULL, dev->devt, NULL, dev->name);
    if (IS_ERR(dev->device)) {
        GPIO_SERIAL_ERROR("Failed to create device: %ld\n", PTR_ERR(dev->device));
        cdev_del(&dev->cdev);
        return PTR_ERR(dev->device);
    }

    /* Setup GPIO pins */
    ret = gpio_serial_setup_gpios(dev);
    if (ret) {
        device_destroy(gpio_serial_class, dev->devt);
        cdev_del(&dev->cdev);
        return ret;
    }

    GPIO_SERIAL_INFO("Device %s initialized successfully (TX: GPIO%d, RX: GPIO%d)\n",
                    dev->name, dev->tx_gpio, dev->rx_gpio);

    return 0;
}

/**
 * gpio_serial_cleanup_device - Cleanup individual device
 * @dev: Device structure
 */
void gpio_serial_cleanup_device(struct gpio_serial_dev *dev)
{
    GPIO_SERIAL_INFO("Cleaning up device %s\n", dev->name);

    /* Cleanup GPIO pins */
    gpio_serial_cleanup_gpios(dev);

    /* Cancel pending work */
    cancel_work_sync(&dev->work);

    /* Remove device from sysfs */
    if (dev->device) {
        device_destroy(gpio_serial_class, dev->devt);
    }

    /* Remove character device */
    cdev_del(&dev->cdev);

    /* Cleanup synchronization primitives */
    mutex_destroy(&dev->mutex);
}

/*
 * Driver Initialization and Cleanup
 */

/**
 * gpio_serial_init - Initialize the driver
 *
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_init(void)
{
    int ret = 0;
    int i;

    printk(KERN_INFO "GPIO Serial Character Device Driver v1.0\n");

    /* Allocate character device region */
    ret = alloc_chrdev_region(&gpio_serial_first_dev, 0, GPIO_SERIAL_NUM_DEVICES, GPIO_SERIAL_NAME);
    if (ret) {
        GPIO_SERIAL_ERROR("Failed to allocate character device region: %d\n", ret);
        return ret;
    }

    gpio_serial_major = MAJOR(gpio_serial_first_dev);
    gpio_serial_devt = gpio_serial_first_dev;

    GPIO_SERIAL_INFO("Character device major number: %d\n", gpio_serial_major);

    /* Create device class */
    gpio_serial_class = class_create(THIS_MODULE, GPIO_SERIAL_CLASS_NAME);
    if (IS_ERR(gpio_serial_class)) {
        GPIO_SERIAL_ERROR("Failed to create device class: %ld\n", PTR_ERR(gpio_serial_class));
        unregister_chrdev_region(gpio_serial_first_dev, GPIO_SERIAL_NUM_DEVICES);
        return PTR_ERR(gpio_serial_class);
    }

    /* Initialize all device instances */
    for (i = 0; i < GPIO_SERIAL_NUM_DEVICES; i++) {
        gpio_serial_devices[i] = kzalloc(sizeof(struct gpio_serial_dev), GFP_KERNEL);
        if (!gpio_serial_devices[i]) {
            GPIO_SERIAL_ERROR("Failed to allocate memory for device %d\n", i);
            ret = -ENOMEM;
            goto cleanup_devices;
        }

        ret = gpio_serial_setup_device(gpio_serial_devices[i], i);
        if (ret) {
            GPIO_SERIAL_ERROR("Failed to setup device %d: %d\n", i, ret);
            kfree(gpio_serial_devices[i]);
            gpio_serial_devices[i] = NULL;
            goto cleanup_devices;
        }
    }

    GPIO_SERIAL_INFO("GPIO Serial driver initialized successfully\n");
    return 0;

cleanup_devices:
    /* Cleanup already initialized devices */
    for (i = 0; i < GPIO_SERIAL_NUM_DEVICES; i++) {
        if (gpio_serial_devices[i]) {
            gpio_serial_cleanup_device(gpio_serial_devices[i]);
            kfree(gpio_serial_devices[i]);
        }
    }

    class_destroy(gpio_serial_class);
    unregister_chrdev_region(gpio_serial_first_dev, GPIO_SERIAL_NUM_DEVICES);
    return ret;
}

/**
 * gpio_serial_exit - Cleanup the driver
 */
void gpio_serial_exit(void)
{
    int i;

    printk(KERN_INFO "GPIO Serial Character Device Driver cleanup\n");

    /* Cleanup all device instances */
    for (i = 0; i < GPIO_SERIAL_NUM_DEVICES; i++) {
        if (gpio_serial_devices[i]) {
            gpio_serial_cleanup_device(gpio_serial_devices[i]);
            kfree(gpio_serial_devices[i]);
            gpio_serial_devices[i] = NULL;
        }
    }

    /* Destroy device class */
    if (gpio_serial_class) {
        class_destroy(gpio_serial_class);
    }

    /* Unregister character device region */
    unregister_chrdev_region(gpio_serial_first_dev, GPIO_SERIAL_NUM_DEVICES);

    GPIO_SERIAL_INFO("GPIO Serial driver cleanup completed\n");
}

/* Module entry and exit points */
module_init(gpio_serial_init);
module_exit(gpio_serial_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Advanced Character Device Driver Project");
MODULE_DESCRIPTION("GPIO Serial Communication Character Device Driver");
MODULE_VERSION("1.0");