/*
 * GPIO Serial Communication Character Device Driver
 * Sysfs Interface Implementation
 *
 * This file implements the sysfs attribute interface for device configuration
 * and monitoring through the /sys filesystem.
 */

#include "gpio_serial.h"

/* Kobject type definition */
struct kobj_type gpio_serial_ktype = {
    .release = NULL,  /* Use default release function */
};

/* Sysfs attribute definitions */
#define GPIO_SERIAL_ATTR_RO(_name) \
    struct gpio_serial_attr gpio_serial_attr_##_name = \
        __ATTR_RO(_name)

#define GPIO_SERIAL_ATTR_RW(_name) \
    struct gpio_serial_attr gpio_serial_attr_##_name = \
        __ATTR(_name, 0644, _name##_show, _name##_store)

/*
 * Device ID Attribute
 */
ssize_t gpio_serial_show_device_id(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->device_id);
}

ssize_t gpio_serial_store_device_id(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int device_id;
    int ret;

    ret = kstrtoint(buf, 10, &device_id);
    if (ret)
        return ret;

    if (device_id < 0 || device_id >= GPIO_SERIAL_NUM_DEVICES)
        return -EINVAL;

    GPIO_SERIAL_INFO("Device %s: device_id attribute not writable (read-only)\n", dev->name);
    return count;
}

/*
 * TX GPIO Attribute
 */
ssize_t gpio_serial_show_tx_gpio(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->tx_gpio);
}

ssize_t gpio_serial_store_tx_gpio(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int gpio;
    int ret;

    ret = kstrtoint(buf, 10, &gpio);
    if (ret)
        return ret;

    if (!gpio_serial_validate_gpio(gpio)) {
        GPIO_SERIAL_ERROR("Device %s: invalid GPIO number %d\n", dev->name, gpio);
        return -EINVAL;
    }

    /* Prevent modification while device is in use */
    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    /* Clean up old GPIO if configured */
    if (gpio_is_valid(dev->tx_gpio)) {
        gpio_free(dev->tx_gpio);
    }

    dev->tx_gpio = gpio;

    /* Request new GPIO */
    ret = gpio_request(gpio, dev->name);
    if (ret) {
        GPIO_SERIAL_ERROR("Device %s: failed to request GPIO %d: %d\n", dev->name, gpio, ret);
        dev->tx_gpio = 0;
        mutex_unlock(&dev->mutex);
        return ret;
    }

    gpio_direction_output(gpio, GPIO_SERIAL_STOP_BIT);

    mutex_unlock(&dev->mutex);

    GPIO_SERIAL_INFO("Device %s: TX GPIO changed to %d\n", dev->name, gpio);
    return count;
}

/*
 * RX GPIO Attribute
 */
ssize_t gpio_serial_show_rx_gpio(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->rx_gpio);
}

ssize_t gpio_serial_store_rx_gpio(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int gpio;
    int ret;

    ret = kstrtoint(buf, 10, &gpio);
    if (ret)
        return ret;

    if (!gpio_serial_validate_gpio(gpio)) {
        GPIO_SERIAL_ERROR("Device %s: invalid GPIO number %d\n", dev->name, gpio);
        return -EINVAL;
    }

    /* Prevent modification while device is in use */
    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    /* Clean up old GPIO if configured */
    if (gpio_is_valid(dev->rx_gpio)) {
        gpio_free(dev->rx_gpio);
    }

    dev->rx_gpio = gpio;

    /* Request new GPIO */
    ret = gpio_request(gpio, dev->name);
    if (ret) {
        GPIO_SERIAL_ERROR("Device %s: failed to request GPIO %d: %d\n", dev->name, gpio, ret);
        dev->rx_gpio = 0;
        mutex_unlock(&dev->mutex);
        return ret;
    }

    gpio_direction_input(gpio);

    mutex_unlock(&dev->mutex);

    GPIO_SERIAL_INFO("Device %s: RX GPIO changed to %d\n", dev->name, gpio);
    return count;
}

/*
 * Device Status Attribute (Read-only)
 */
ssize_t gpio_serial_show_status(struct gpio_serial_dev *dev, char *buf)
{
    ssize_t len = 0;
    unsigned long flags;
    int available;

    spin_lock_irqsave(&dev->spinlock, flags);
    available = (dev->rx_head - dev->rx_tail + sizeof(dev->rx_buffer)) % sizeof(dev->rx_buffer);
    spin_unlock_irqrestore(&dev->spinlock, flags);

    len += sprintf(buf + len, "Device: %s\n", dev->name);
    len += sprintf(buf + len, "TX GPIO: %d\n", dev->tx_gpio);
    len += sprintf(buf + len, "RX GPIO: %d\n", dev->rx_gpio);
    len += sprintf(buf + len, "IRQ Number: %d\n", dev->irq_number);
    len += sprintf(buf + len, "Blocking Mode: %s\n", dev->blocking_mode ? "enabled" : "disabled");
    len += sprintf(buf + len, "Echo Mode: %s\n", dev->echo_mode ? "enabled" : "disabled");
    len += sprintf(buf + len, "Timeout: %d jiffies\n", dev->timeout);
    len += sprintf(buf + len, "Data Available: %d bytes\n", available);
    len += sprintf(buf + len, "In Frame: %s\n", dev->in_frame ? "yes" : "no");
    len += sprintf(buf + len, "Bit Position: %d\n", dev->bit_position);

    return len;
}

/*
 * Device Statistics Attribute (Read-only)
 */
ssize_t gpio_serial_show_statistics(struct gpio_serial_dev *dev, char *buf)
{
    ssize_t len = 0;

    len += sprintf(buf + len, "Bytes Received: %lu\n", dev->bytes_received);
    len += sprintf(buf + len, "Bytes Sent: %lu\n", dev->bytes_sent);
    len += sprintf(buf + len, "Errors: %lu\n", dev->errors);
    len += sprintf(buf + len, "Interrupts: %lu\n", dev->interrupts);
    len += sprintf(buf + len, "Current Byte: 0x%02x\n", dev->current_byte);

    return len;
}

/*
 * Blocking Mode Attribute
 */
ssize_t gpio_serial_show_blocking_mode(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->blocking_mode ? 1 : 0);
}

ssize_t gpio_serial_store_blocking_mode(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int mode;
    int ret;

    ret = kstrtoint(buf, 10, &mode);
    if (ret)
        return ret;

    dev->blocking_mode = (mode != 0);

    GPIO_SERIAL_INFO("Device %s: blocking mode %s\n", 
                    dev->name, dev->blocking_mode ? "enabled" : "disabled");

    return count;
}

/*
 * Timeout Attribute
 */
ssize_t gpio_serial_show_timeout(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->timeout);
}

ssize_t gpio_serial_store_timeout(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int timeout;
    int ret;

    ret = kstrtoint(buf, 10, &timeout);
    if (ret)
        return ret;

    if (timeout < 0 || timeout > 60 * HZ) {
        GPIO_SERIAL_ERROR("Device %s: timeout %d out of range (0 to %d)\n", 
                         dev->name, timeout, 60 * HZ);
        return -EINVAL;
    }

    dev->timeout = timeout;

    GPIO_SERIAL_INFO("Device %s: timeout set to %d jiffies\n", dev->name, timeout);
    return count;
}

/*
 * Echo Mode Attribute
 */
ssize_t gpio_serial_show_echo_mode(struct gpio_serial_dev *dev, char *buf)
{
    return sprintf(buf, "%d\n", dev->echo_mode ? 1 : 0);
}

ssize_t gpio_serial_store_echo_mode(struct gpio_serial_dev *dev, const char *buf, size_t count)
{
    int mode;
    int ret;

    ret = kstrtoint(buf, 10, &mode);
    if (ret)
        return ret;

    dev->echo_mode = (mode != 0);

    GPIO_SERIAL_INFO("Device %s: echo mode %s\n", 
                    dev->name, dev->echo_mode ? "enabled" : "disabled");

    return count;
}

/* Define sysfs attributes */
static GPIO_SERIAL_ATTR_RO(device_id);
static GPIO_SERIAL_ATTR_RW(tx_gpio);
static GPIO_SERIAL_ATTR_RW(rx_gpio);
static GPIO_SERIAL_ATTR_RO(status);
static GPIO_SERIAL_ATTR_RO(statistics);
static GPIO_SERIAL_ATTR_RW(blocking_mode);
static GPIO_SERIAL_ATTR_RW(timeout);
static GPIO_SERIAL_ATTR_RW(echo_mode);

/* Attribute array */
static struct attribute *gpio_serial_attrs[] = {
    &gpio_serial_attr_device_id.attr,
    &gpio_serial_attr_tx_gpio.attr,
    &gpio_serial_attr_rx_gpio.attr,
    &gpio_serial_attr_status.attr,
    &gpio_serial_attr_statistics.attr,
    &gpio_serial_attr_blocking_mode.attr,
    &gpio_serial_attr_timeout.attr,
    &gpio_serial_attr_echo_mode.attr,
    NULL,
};

/* Attribute group */
struct attribute_group gpio_serial_attr_group = {
    .attrs = gpio_serial_attrs,
    .name = NULL,  /* Use device name as group name */
};

/*
 * Utility Functions
 */

/**
 * gpio_serial_validate_gpio - Validate GPIO number
 * @gpio: GPIO number to validate
 *
 * Returns: true if GPIO is valid, false otherwise
 */
int gpio_serial_validate_gpio(int gpio)
{
    /* Basic GPIO validation - adjust based on your platform */
    return (gpio >= 0 && gpio <= 53);  /* Raspberry Pi GPIO range */
}

/**
 * gpio_serial_update_statistics - Update device statistics
 * @dev: Device structure
 * @type: Type of operation (0=receive, 1=send, 2=error)
 */
void gpio_serial_update_statistics(struct gpio_serial_dev *dev, int type)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->spinlock, flags);

    switch (type) {
    case 0: /* Receive */
        dev->bytes_received++;
        break;
    case 1: /* Send */
        dev->bytes_sent++;
        break;
    case 2: /* Error */
        dev->errors++;
        break;
    }

    spin_unlock_irqrestore(&dev->spinlock, flags);
}

/**
 * gpio_serial_log_error - Log error message
 * @dev: Device structure
 * @format: Format string
 * @...: Variable arguments
 */
void gpio_serial_log_error(struct gpio_serial_dev *dev, const char *format, ...)
{
    va_list args;
    char buffer[256];

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    printk(KERN_ERR "[GPIO_SERIAL %s] %s", dev->name, buffer);
}

/*
 * Sysfs Interface Initialization and Cleanup
 */

/**
 * gpio_serial_sysfs_init - Initialize sysfs interface for device
 * @dev: Device structure
 *
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_sysfs_init(struct gpio_serial_dev *dev)
{
    int ret;

    /* Initialize kobject */
    memset(&dev->kobj, 0, sizeof(dev->kobj));

    /* Add device attributes to sysfs */
    ret = sysfs_create_group(&dev->kobj, &gpio_serial_attr_group);
    if (ret) {
        GPIO_SERIAL_ERROR("Device %s: failed to create sysfs group: %d\n", dev->name, ret);
        return ret;
    }

    GPIO_SERIAL_DEBUG("Device %s: sysfs interface initialized\n", dev->name);
    return 0;
}

/**
 * gpio_serial_sysfs_exit - Cleanup sysfs interface for device
 * @dev: Device structure
 */
void gpio_serial_sysfs_exit(struct gpio_serial_dev *dev)
{
    /* Remove sysfs group */
    sysfs_remove_group(&dev->kobj, &gpio_serial_attr_group);

    GPIO_SERIAL_DEBUG("Device %s: sysfs interface cleaned up\n", dev->name);
}

/*
 * IOCTL Command Definitions
 * These should be defined in the header file but are included here for completeness
 */

#ifndef GPIO_SERIAL_RESET
#define GPIO_SERIAL_RESET _IO('G', 0x01)
#endif

#ifndef GPIO_SERIAL_GET_STATS
#define GPIO_SERIAL_GET_STATS _IOR('G', 0x02, unsigned long)
#endif

#ifndef GPIO_SERIAL_CLEAR_STATS
#define GPIO_SERIAL_CLEAR_STATS _IO('G', 0x03)
#endif