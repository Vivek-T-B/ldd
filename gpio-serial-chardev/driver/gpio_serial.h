/*
 * GPIO Serial Communication Character Device Driver
 * Header file with common definitions and data structures
 *
 * This driver implements a character device for GPIO-based serial communication
 * with support for multiple device instances, blocking I/O, and sysfs interface.
 */

#ifndef GPIO_SERIAL_H
#define GPIO_SERIAL_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#define GPIO_SERIAL_MAJOR         0    /* Dynamic allocation */
#define GPIO_SERIAL_NUM_DEVICES   3    /* Support up to 3 devices */
#define GPIO_SERIAL_NAME          "gpio_serial"
#define GPIO_SERIAL_CLASS_NAME    "gpio_serial_class"

/* GPIO communication protocol defines */
#define GPIO_SERIAL_BITS_PER_BYTE     8
#define GPIO_SERIAL_START_BIT         0    /* Start bit (logical 0) */
#define GPIO_SERIAL_STOP_BIT          1    /* Stop bit (logical 1) */
#define GPIO_SERIAL_PARITY_BIT        0    /* No parity for simplicity */
#define GPIO_SERIAL_BAUDRATE_DELAY    100  /* Delay in microseconds for bit timing */

/* Device configuration structure */
struct gpio_serial_dev {
    dev_t devt;                    /* Device number */
    struct cdev cdev;             /* Character device structure */
    struct device *device;        /* Device structure for sysfs */
    struct class *class;          /* Device class */
    struct kobject kobj;          /* Kobject for sysfs */

    /* Device identification */
    int device_id;                /* Unique device instance ID */
    char name[32];                /* Device name */

    /* GPIO pins configuration */
    int tx_gpio;                  /* TX GPIO pin number */
    int rx_gpio;                  /* RX GPIO pin number */
    int irq_number;               /* IRQ number for RX pin */

    /* Communication state */
    unsigned char rx_buffer[256]; /* RX buffer */
    int rx_head;                  /* RX buffer head */
    int rx_tail;                  /* RX buffer tail */
    bool data_available;          /* Flag for data availability */

    /* Synchronization primitives */
    spinlock_t spinlock;          /* Spinlock for interrupt context */
    struct mutex mutex;           /* Mutex for blocking context */
    wait_queue_head_t read_wait;  /* Wait queue for blocking reads */

    /* Device statistics */
    unsigned long bytes_sent;     /* Total bytes transmitted */
    unsigned long bytes_received; /* Total bytes received */
    unsigned long errors;         /* Error count */
    unsigned long interrupts;     /* Interrupt count */

    /* Device configuration */
    bool blocking_mode;           /* Blocking vs non-blocking mode */
    unsigned int timeout;         /* Read timeout in jiffies */
    bool echo_mode;               /* Echo received data back */

    /* Protocol state machine */
    unsigned char current_byte;   /* Current byte being received */
    int bit_position;             /* Current bit position (0-7) */
    unsigned long last_bit_time;  /* Time of last bit reception */
    bool in_frame;                /* Currently receiving a frame */

    /* Work queue for deferred processing */
    struct work_struct work;      /* Work structure for deferred processing */
};

/* File operations structure */
extern const struct file_operations gpio_serial_fops;

/* Driver initialization and cleanup functions */
int gpio_serial_init(void);
void gpio_serial_exit(void);

/* Device management functions */
int gpio_serial_setup_device(struct gpio_serial_dev *dev, int device_id);
void gpio_serial_cleanup_device(struct gpio_serial_dev *dev);

/* GPIO management functions */
int gpio_serial_setup_gpios(struct gpio_serial_dev *dev);
void gpio_serial_cleanup_gpios(struct gpio_serial_dev *dev);
void gpio_serial_write_bit(struct gpio_serial_dev *dev, int bit);
int gpio_serial_read_bit(struct gpio_serial_dev *dev);

/* Protocol functions */
void gpio_serial_send_byte(struct gpio_serial_dev *dev, unsigned char byte);
void gpio_serial_protocol_init(struct gpio_serial_dev *dev);
void gpio_serial_protocol_reset(struct gpio_serial_dev *dev);

/* Interrupt handler */
irqreturn_t gpio_serial_irq_handler(int irq, void *dev_id);

/* Work queue handler */
void gpio_serial_work_handler(struct work_struct *work);

/* Sysfs attribute structures */
extern struct kobj_type gpio_serial_ktype;

/* Sysfs attribute show/store functions */
ssize_t gpio_serial_show_device_id(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_device_id(struct gpio_serial_dev *dev, const char *buf, size_t count);

ssize_t gpio_serial_show_tx_gpio(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_tx_gpio(struct gpio_serial_dev *dev, const char *buf, size_t count);

ssize_t gpio_serial_show_rx_gpio(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_rx_gpio(struct gpio_serial_dev *dev, const char *buf, size_t count);

ssize_t gpio_serial_show_status(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_show_statistics(struct gpio_serial_dev *dev, char *buf);

ssize_t gpio_serial_show_blocking_mode(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_blocking_mode(struct gpio_serial_dev *dev, const char *buf, size_t count);

ssize_t gpio_serial_show_timeout(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_timeout(struct gpio_serial_dev *dev, const char *buf, size_t count);

ssize_t gpio_serial_show_echo_mode(struct gpio_serial_dev *dev, char *buf);
ssize_t gpio_serial_store_echo_mode(struct gpio_serial_dev *dev, const char *buf, size_t count);

/* Utility functions */
int gpio_serial_validate_gpio(int gpio);
void gpio_serial_update_statistics(struct gpio_serial_dev *dev, int type);
void gpio_serial_log_error(struct gpio_serial_dev *dev, const char *format, ...);

/* Module parameters */
extern int debug_level;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug level (0=quiet, 1=normal, 2=verbose)");

/* Debug macros */
#define GPIO_SERIAL_DEBUG(fmt, ...) \
    do { \
        if (debug_level >= 2) \
            printk(KERN_DEBUG "[GPIO_SERIAL] " fmt, ##__VA_ARGS__); \
    } while (0)

#define GPIO_SERIAL_INFO(fmt, ...) \
    do { \
        if (debug_level >= 1) \
            printk(KERN_INFO "[GPIO_SERIAL] " fmt, ##__VA_ARGS__); \
    } while (0)

#define GPIO_SERIAL_ERROR(fmt, ...) \
    printk(KERN_ERR "[GPIO_SERIAL ERROR] " fmt, ##__VA_ARGS__)

#define GPIO_SERIAL_WARN(fmt, ...) \
    printk(KERN_WARNING "[GPIO_SERIAL WARNING] " fmt, ##__VA_ARGS__)

/* Device structure arrays */
extern struct gpio_serial_dev *gpio_serial_devices[GPIO_SERIAL_NUM_DEVICES];
extern struct class *gpio_serial_class;
extern dev_t gpio_serial_devt;

/* Character device region */
extern dev_t gpio_serial_first_dev;
extern int gpio_serial_major;

/* Function prototypes for device operations */
int gpio_serial_open(struct inode *inode, struct file *filp);
int gpio_serial_release(struct inode *inode, struct file *filp);
ssize_t gpio_serial_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t gpio_serial_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
long gpio_serial_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Helper macros */
#define GPIO_SERIAL_GET_DEV(filp) \
    container_of(filp->private_data, struct gpio_serial_dev, cdev)

#define GPIO_SERIAL_MIN(a, b) ((a) < (b) ? (a) : (b))

#endif /* GPIO_SERIAL_H */