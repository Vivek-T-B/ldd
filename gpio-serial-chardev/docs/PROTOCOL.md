# GPIO Serial Communication Protocol Specification

## Overview

This document describes the GPIO-based serial communication protocol implemented in the character device driver. The protocol uses bit-banging techniques to simulate serial communication over GPIO pins, demonstrating low-level hardware control and protocol implementation in kernel space.

## Protocol Fundamentals

### Basic Concept

The protocol implements a simple asynchronous serial communication using GPIO pins as TX (transmit) and RX (receive) lines. Instead of using dedicated UART hardware, the driver manually controls GPIO voltage levels to represent serial data bits.

### Frame Structure

Each data frame consists of:
```
[START_BIT] [BIT_0] [BIT_1] [BIT_2] [BIT_3] [BIT_4] [BIT_5] [BIT_6] [BIT_7] [STOP_BIT]
     0           LSB                                                            
```

**Bit Definitions**:
- **START_BIT**: Logical 0 (low voltage) - indicates frame start
- **BIT_0-7**: Data bits (LSB first, i.e., least significant bit transmitted first)
- **STOP_BIT**: Logical 1 (high voltage) - indicates frame end

### Electrical Characteristics

- **Voltage Levels**: 
  - Logical 0: 0V (GPIO low)
  - Logical 1: 3.3V/5V (GPIO high, platform dependent)
- **Baud Rate**: Configurable (default 9600 bps, 100μs bit time)
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)

## Protocol State Machine

### Receiver State Machine

The protocol uses a state machine to accurately detect and decode incoming serial frames:

```c
enum gpio_serial_rx_state {
    RX_STATE_IDLE,         /* No frame being received */
    RX_STATE_START_BIT,    /* Detecting start bit */
    RX_STATE_DATA_BITS,    /* Receiving data bits */
    RX_STATE_STOP_BIT,     /* Detecting stop bit */
    RX_STATE_COMPLETE      /* Frame complete, ready for next */
};

struct gpio_serial_dev {
    enum gpio_serial_rx_state rx_state;
    unsigned char current_byte;
    int bit_position;
    unsigned long last_bit_time;
    bool in_frame;
};
```

### State Transitions

```
                    ┌─────────────┐
                    │   IDLE      │
                    │  (Waiting   │
                    │ for start)  │
                    └──────┬──────┘
                           │ Start bit detected (low)
                           ▼
                    ┌─────────────┐
                    │ START_BIT   │
                    │ (Validate   │
                    │  start bit) │
                    └──────┬──────┘
                           │ Valid start bit
                           ▼
                    ┌─────────────┐     ┌─────────────┐
                    │  DATA_BITS  │────▶│  STOP_BIT   │
                    │ (Receiving  │     │ (Validate   │
                    │  8 bits)    │     │  stop bit)  │
                    └──────┬──────┘     └──────┬──────┘
                           │ All bits received  │ Valid stop bit
                           ▼                    ▼
                    ┌─────────────┐     ┌─────────────┐
                    │   ERROR     │     │  COMPLETE   │
                    │ (Frame      │     │ (Byte       │
                    │  error)     │     │  ready)     │
                    └─────────────┘     └─────────────┘
                                              │
                                              ▼
                                         ┌─────────────┐
                                         │    IDLE     │
                                         │ (Next frame)│
                                         └─────────────┘
```

### Timing Validation

Proper timing is crucial for reliable communication:

```c
/* Timing parameters */
#define GPIO_SERIAL_BIT_TIME_US     100    /* 10ms for 9600 baud */
#define GPIO_SERIAL_BIT_TOLERANCE   10     /* ±10% timing tolerance */

/* Validate bit timing */
static bool validate_bit_timing(struct gpio_serial_dev *dev, unsigned long current_time)
{
    unsigned long elapsed = jiffies_to_usecs(current_time - dev->last_bit_time);
    unsigned long expected = GPIO_SERIAL_BIT_TIME_US;
    unsigned long min_time = expected * (100 - GPIO_SERIAL_BIT_TOLERANCE) / 100;
    unsigned long max_time = expected * (100 + GPIO_SERIAL_BIT_TOLERANCE) / 100;
    
    return (elapsed >= min_time && elapsed <= max_time);
}
```

## Implementation Details

### Transmitter Implementation

**Bit-Banging Transmission**:

```c
/**
 * gpio_serial_send_byte - Transmit a complete byte using bit-banging
 * @dev: Device structure
 * @byte: Byte to transmit
 */
void gpio_serial_send_byte(struct gpio_serial_dev *dev, unsigned char byte)
{
    int i;
    
    GPIO_SERIAL_DEBUG("Transmitting byte: 0x%02x\n", byte);
    
    /* Critical section - disable interrupts during transmission */
    unsigned long flags;
    local_irq_save(flags);
    
    /* Transmit start bit (logical 0) */
    gpio_serial_write_bit(dev, GPIO_SERIAL_START_BIT);  /* 0 */
    
    /* Transmit data bits (LSB first) */
    for (i = 0; i < GPIO_SERIAL_BITS_PER_BYTE; i++) {
        gpio_serial_write_bit(dev, (byte >> i) & 0x01);
    }
    
    /* Transmit stop bit (logical 1) */
    gpio_serial_write_bit(dev, GPIO_SERIAL_STOP_BIT);   /* 1 */
    
    /* Additional stop bit for timing margin */
    gpio_serial_write_bit(dev, GPIO_SERIAL_STOP_BIT);   /* 1 */
    
    local_irq_restore(flags);
    
    GPIO_SERIAL_DEBUG("Transmission completed\n");
}

/**
 * gpio_serial_write_bit - Write a single bit with precise timing
 * @dev: Device structure
 * @bit: Bit value (0 or 1)
 */
void gpio_serial_write_bit(struct gpio_serial_dev *dev, int bit)
{
    /* Set GPIO to appropriate level */
    gpio_set_value(dev->tx_gpio, bit);
    
    /* Precise timing delay */
    udelay(GPIO_SERIAL_BAUDRATE_DELAY);  /* 100μs for 9600 baud */
}
```

### Receiver Implementation

**Interrupt-Driven Reception**:

```c
/**
 * gpio_serial_irq_handler - Handle GPIO interrupt for bit reception
 * @irq: Interrupt request number
 * @dev_id: Device identifier
 * 
 * Returns: IRQ_HANDLED if interrupt processed
 */
irqreturn_t gpio_serial_irq_handler(int irq, void *dev_id)
{
    struct gpio_serial_dev *dev = dev_id;
    unsigned long flags;
    int bit_value;
    unsigned long current_time = jiffies;
    
    GPIO_SERIAL_DEBUG("IRQ triggered for device %s, state: %d\n", 
                     dev->name, dev->rx_state);
    
    spin_lock_irqsave(&dev->spinlock, flags);
    
    dev->interrupts++;
    
    /* State machine processing */
    switch (dev->rx_state) {
    case RX_STATE_IDLE:
        /* Check for start bit */
        bit_value = gpio_serial_read_bit(dev);
        if (bit_value == GPIO_SERIAL_START_BIT) {
            dev->rx_state = RX_STATE_START_BIT;
            dev->current_byte = 0;
            dev->bit_position = 0;
            dev->last_bit_time = current_time;
            GPIO_SERIAL_DEBUG("Start bit detected\n");
        }
        break;
        
    case RX_STATE_START_BIT:
        /* Validate start bit timing */
        if (!validate_bit_timing(dev, current_time)) {
            GPIO_SERIAL_DEBUG("Start bit timing error, resetting\n");
            dev->rx_state = RX_STATE_IDLE;
            dev->errors++;
            break;
        }
        
        /* Move to data bits */
        dev->rx_state = RX_STATE_DATA_BITS;
        dev->last_bit_time = current_time;
        GPIO_SERIAL_DEBUG("Start bit valid, receiving data bits\n");
        break;
        
    case RX_STATE_DATA_BITS:
        /* Validate bit timing */
        if (!validate_bit_timing(dev, current_time)) {
            GPIO_SERIAL_DEBUG("Data bit timing error, resetting\n");
            dev->rx_state = RX_STATE_IDLE;
            dev->errors++;
            break;
        }
        
        /* Read current bit */
        bit_value = gpio_serial_read_bit(dev);
        
        /* Store bit in correct position (LSB first) */
        if (bit_value) {
            dev->current_byte |= (1 << dev->bit_position);
        }
        
        dev->bit_position++;
        dev->last_bit_time = current_time;
        
        /* Check if all data bits received */
        if (dev->bit_position >= GPIO_SERIAL_BITS_PER_BYTE) {
            dev->rx_state = RX_STATE_STOP_BIT;
            GPIO_SERIAL_DEBUG("All data bits received: 0x%02x\n", dev->current_byte);
        }
        break;
        
    case RX_STATE_STOP_BIT:
        /* Validate stop bit timing */
        if (!validate_bit_timing(dev, current_time)) {
            GPIO_SERIAL_DEBUG("Stop bit timing error, resetting\n");
            dev->rx_state = RX_STATE_IDLE;
            dev->errors++;
            break;
        }
        
        /* Check for stop bit (should be logical 1) */
        bit_value = gpio_serial_read_bit(dev);
        if (bit_value == GPIO_SERIAL_STOP_BIT) {
            /* Frame complete - add byte to buffer */
            dev->rx_buffer[dev->rx_head] = dev->current_byte;
            dev->rx_head = (dev->rx_head + 1) % sizeof(dev->rx_buffer);
            dev->data_available = true;
            
            /* Wake up waiting readers */
            wake_up_interruptible(&dev->read_wait);
            
            /* Echo mode - transmit back the received byte */
            if (dev->echo_mode) {
                gpio_serial_send_byte(dev, dev->current_byte);
            }
            
            GPIO_SERIAL_DEBUG("Frame complete, byte 0x%02x added to buffer\n", 
                             dev->current_byte);
            
            dev->bytes_received++;
        } else {
            GPIO_SERIAL_DEBUG("Invalid stop bit, frame error\n");
            dev->errors++;
        }
        
        /* Reset for next frame */
        dev->rx_state = RX_STATE_IDLE;
        dev->bit_position = 0;
        break;
        
    default:
        /* Invalid state - reset */
        dev->rx_state = RX_STATE_IDLE;
        dev->errors++;
        break;
    }
    
    spin_unlock_irqrestore(&dev->spinlock, flags);
    
    return IRQ_HANDLED;
}
```

### Polling Mode Implementation

When interrupt-driven mode is not available, the driver supports polling:

```c
/**
 * gpio_serial_poll_receive - Poll for received data
 * @dev: Device structure
 * 
 * Called periodically to check for received data
 */
static void gpio_serial_poll_receive(struct gpio_serial_dev *dev)
{
    int bit_value;
    unsigned long current_time = jiffies;
    
    /* This function would be called from a kernel timer or work queue */
    
    spin_lock(&dev->spinlock);
    
    /* Check if we're in the middle of receiving a frame */
    if (dev->rx_state == RX_STATE_IDLE) {
        /* Check for start bit */
        bit_value = gpio_serial_read_bit(dev);
        if (bit_value == GPIO_SERIAL_START_BIT) {
            dev->rx_state = RX_STATE_START_BIT;
            dev->current_byte = 0;
            dev->bit_position = 0;
            dev->last_bit_time = current_time;
        }
    } else {
        /* Check if enough time has passed for the next bit */
        if (time_after(current_time, dev->last_bit_time + 
                      usecs_to_jiffies(GPIO_SERIAL_BAUDRATE_DELAY))) {
            
            /* Process current bit based on state */
            bit_value = gpio_serial_read_bit(dev);
            
            switch (dev->rx_state) {
            case RX_STATE_START_BIT:
                if (bit_value == 0) {  /* Valid start bit */
                    dev->rx_state = RX_STATE_DATA_BITS;
                    dev->bit_position = 0;
                    dev->last_bit_time = current_time;
                } else {
                    dev->rx_state = RX_STATE_IDLE;  /* False start */
                }
                break;
                
            case RX_STATE_DATA_BITS:
                if (bit_value) {
                    dev->current_byte |= (1 << dev->bit_position);
                }
                dev->bit_position++;
                
                if (dev->bit_position >= GPIO_SERIAL_BITS_PER_BYTE) {
                    dev->rx_state = RX_STATE_STOP_BIT;
                }
                dev->last_bit_time = current_time;
                break;
                
            case RX_STATE_STOP_BIT:
                if (bit_value == 1) {  /* Valid stop bit */
                    /* Complete frame received */
                    dev->rx_buffer[dev->rx_head] = dev->current_byte;
                    dev->rx_head = (dev->rx_head + 1) % sizeof(dev->rx_buffer);
                    dev->data_available = true;
                    wake_up_interruptible(&dev->read_wait);
                    
                    if (dev->echo_mode) {
                        gpio_serial_send_byte(dev, dev->current_byte);
                    }
                }
                dev->rx_state = RX_STATE_IDLE;
                break;
            }
        }
    }
    
    spin_unlock(&dev->spinlock);
}
```

## Error Detection and Handling

### Frame Error Detection

```c
/**
 * gpio_serial_validate_frame - Validate received frame
 * @dev: Device structure
 * @byte: Received byte
 * 
 * Returns: true if frame is valid, false otherwise
 */
static bool gpio_serial_validate_frame(struct gpio_serial_dev *dev, 
                                      unsigned char byte)
{
    /* Frame validation logic */
    
    /* Check for framing errors */
    if (dev->rx_state != RX_STATE_COMPLETE) {
        return false;
    }
    
    /* Check for buffer overflow */
    if ((dev->rx_head + 1) % sizeof(dev->rx_buffer) == dev->rx_tail) {
        GPIO_SERIAL_ERROR("RX buffer overflow\n");
        dev->errors++;
        return false;
    }
    
    /* Additional validation based on protocol */
    
    return true;
}

/**
 * gpio_serial_protocol_reset - Reset protocol state machine
 * @dev: Device structure
 * 
 * Called to reset the protocol state machine after errors
 */
void gpio_serial_protocol_reset(struct gpio_serial_dev *dev)
{
    unsigned long flags;
    
    spin_lock_irqsave(&dev->spinlock, flags);
    
    /* Reset protocol state */
    dev->rx_state = RX_STATE_IDLE;
    dev->current_byte = 0;
    dev->bit_position = 0;
    dev->in_frame = false;
    dev->last_bit_time = 0;
    
    /* Clear data availability */
    dev->data_available = false;
    dev->rx_head = 0;
    dev->rx_tail = 0;
    
    spin_unlock_irqrestore(&dev->spinlock, flags);
    
    GPIO_SERIAL_INFO("Protocol state machine reset for device %s\n", dev->name);
}
```

## Performance Characteristics

### Timing Constraints

**Bit Timing Accuracy**:
- **Target**: ±1% timing accuracy for reliable communication
- **Interrupt Latency**: Must be < 10μs for 9600 baud operation
- **GPIO Access Time**: Typical GPIO toggle time ~1-5μs

**Baud Rate Limitations**:
```c
/* Calculate maximum reliable baud rate */
static unsigned int calculate_max_baud_rate(struct gpio_serial_dev *dev)
{
    unsigned long interrupt_latency, gpio_access_time;
    
    /* Platform-specific measurements */
    interrupt_latency = measure_interrupt_latency();
    gpio_access_time = measure_gpio_access_time();
    
    /* Conservative calculation */
    unsigned long bit_time = interrupt_latency + gpio_access_time * 2 + 50; /* 50μs margin */
    unsigned long max_baud = 1000000 / bit_time;  /* Convert to baud */
    
    return max_baud;
}
```

### Throughput Analysis

**Theoretical Maximum**:
- **9600 baud**: 960 bytes/second (8N1 format)
- **Actual throughput**: ~80% of theoretical due to protocol overhead

**Bottlenecks**:
1. **GPIO access time**: Hardware limitation
2. **Interrupt latency**: System load dependent
3. **Kernel scheduling**: Process priority dependent
4. **Memory copy overhead**: Buffer management cost

## Configuration Options

### Baud Rate Configuration

```c
/**
 * gpio_serial_set_baud_rate - Configure communication baud rate
 * @dev: Device structure
 * @baud_rate: Desired baud rate
 * 
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_set_baud_rate(struct gpio_serial_dev *dev, unsigned int baud_rate)
{
    if (baud_rate < 300 || baud_rate > 115200) {
        return -EINVAL;
    }
    
    /* Calculate bit time in microseconds */
    unsigned long bit_time_us = 1000000 / baud_rate;
    
    /* Update timing parameters */
    dev->bit_time_us = bit_time_us;
    dev->bit_time_jiffies = usecs_to_jiffies(bit_time_us);
    
    GPIO_SERIAL_INFO("Device %s: baud rate set to %u bps (bit time: %lu μs)\n",
                    dev->name, baud_rate, bit_time_us);
    
    return 0;
}
```

### Frame Format Configuration

```c
/**
 * gpio_serial_set_frame_format - Configure frame format
 * @dev: Device structure
 * @data_bits: Number of data bits (5-8)
 * @parity: Parity type (none, even, odd)
 * @stop_bits: Number of stop bits (1-2)
 * 
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_set_frame_format(struct gpio_serial_dev *dev,
                                int data_bits, int parity, int stop_bits)
{
    if (data_bits < 5 || data_bits > 8) {
        return -EINVAL;
    }
    
    if (stop_bits < 1 || stop_bits > 2) {
        return -EINVAL;
    }
    
    dev->data_bits = data_bits;
    dev->parity = parity;
    dev->stop_bits = stop_bits;
    
    GPIO_SERIAL_INFO("Device %s: frame format %d%c%d\n",
                    dev->name, data_bits, 
                    parity == 0 ? 'N' : (parity > 0 ? 'E' : 'O'),
                    stop_bits);
    
    return 0;
}
```

## Advanced Features

### Flow Control

```c
/**
 * gpio_serial_flow_control - Implement software flow control
 * @dev: Device structure
 * @control_byte: Flow control byte
 * 
 * Returns: 0 on success, negative error code on failure
 */
int gpio_serial_flow_control(struct gpio_serial_dev *dev, unsigned char control_byte)
{
    switch (control_byte) {
    case 0x11:  /* XON - resume transmission */
        dev->flow_control = FLOW_CONTROL_XON;
        GPIO_SERIAL_DEBUG("XON received - resume transmission\n");
        break;
        
    case 0x13:  /* XOFF - pause transmission */
        dev->flow_control = FLOW_CONTROL_XOFF;
        GPIO_SERIAL_DEBUG("XOFF received - pause transmission\n");
        break;
        
    default:
        return -EINVAL;
    }
    
    return 0;
}
```

### Multi-Device Coordination

```c
/**
 * gpio_serial_coordinate_devices - Coordinate multiple device instances
 * @dev_list: Array of device structures
 * @num_devices: Number of devices
 * 
 * Ensures synchronized operation across multiple devices
 */
static int gpio_serial_coordinate_devices(struct gpio_serial_dev **dev_list,
                                         int num_devices)
{
    int i, ret = 0;
    
    /* Acquire global coordination lock */
    if (mutex_lock_interruptible(&global_device_mutex))
        return -ERESTARTSYS;
    
    /* Coordinate device operations */
    for (i = 0; i < num_devices; i++) {
        ret = gpio_serial_prepare_device(dev_list[i]);
        if (ret) {
            /* Rollback previous preparations */
            while (--i >= 0) {
                gpio_serial_unprepare_device(dev_list[i]);
            }
            break;
        }
    }
    
    mutex_unlock(&global_device_mutex);
    return ret;
}
```

## Testing and Validation

### Protocol Validation Tests

```c
/**
 * gpio_serial_protocol_test - Validate protocol implementation
 * @dev: Device structure
 * 
 * Returns: 0 if all tests pass, negative error code on failure
 */
static int gpio_serial_protocol_test(struct gpio_serial_dev *dev)
{
    unsigned char test_byte, received_byte;
    int i, errors = 0;
    
    /* Test all possible byte values */
    for (i = 0; i < 256; i++) {
        test_byte = (unsigned char)i;
        
        /* Transmit test byte */
        gpio_serial_send_byte(dev, test_byte);
        
        /* Wait for reception */
        msleep(10);
        
        /* Check if byte was received correctly */
        if (dev->rx_buffer[dev->rx_tail] == test_byte) {
            GPIO_SERIAL_DEBUG("Test byte 0x%02x: PASS\n", test_byte);
        } else {
            GPIO_SERIAL_ERROR("Test byte 0x%02x: FAIL (got 0x%02x)\n",
                             test_byte, dev->rx_buffer[dev->rx_tail]);
            errors++;
        }
    }
    
    return errors ? -EIO : 0;
}
```

### Performance Benchmarking

```c
/**
 * gpio_serial_performance_test - Benchmark protocol performance
 * @dev: Device structure
 * @test_duration: Test duration in seconds
 * 
 * Returns: Performance statistics
 */
static struct gpio_serial_perf_stats gpio_serial_performance_test(
    struct gpio_serial_dev *dev, int test_duration)
{
    struct gpio_serial_perf_stats stats = {0};
    unsigned char buffer[1024];
    ktime_t start_time, end_time;
    s64 elapsed_ns;
    
    start_time = ktime_get();
    
    /* Continuous transmit/receive test */
    for (int i = 0; i < test_duration * 100; i++) {  /* 100 iterations per second */
        /* Fill buffer with test data */
        memset(buffer, 'A' + (i % 26), sizeof(buffer));
        
        /* Transmit buffer */
        gpio_serial_send_buffer(dev, buffer, sizeof(buffer));
        
        /* Attempt to receive data */
        gpio_serial_receive_buffer(dev, buffer, sizeof(buffer));
        
        stats.bytes_transmitted += sizeof(buffer);
        stats.bytes_received += sizeof(buffer);
    }
    
    end_time = ktime_get();
    elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    /* Calculate performance metrics */
    stats.elapsed_seconds = elapsed_ns / 1000000000.0;
    stats.bytes_per_second = stats.bytes_transmitted / stats.elapsed_seconds;
    stats.baud_rate_achieved = stats.bytes_per_second * 10;  /* 8N1 = 10 bits per byte */
    
    return stats;
}
```

## Conclusion

This GPIO-based serial communication protocol demonstrates:

1. **Low-level hardware control** through GPIO manipulation
2. **Robust state machine implementation** for reliable data reception
3. **Precise timing control** for bit-banging communication
4. **Comprehensive error handling** and recovery mechanisms
5. **Performance optimization** for practical communication speeds

The protocol serves as an excellent example of implementing custom communication protocols in kernel space, showcasing the level of detail and precision required for reliable hardware interfacing in Linux device drivers.