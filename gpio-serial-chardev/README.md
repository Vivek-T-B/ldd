# GPIO Serial Character Device Driver

## Advanced Character Device Driver with Multi-Faceted Features

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Linux Kernel](https://img.shields.io/badge/Linux%20Kernel-4.0%2B-green.svg)](https://www.kernel.org/)
[![C](https://img.shields.io/badge/Language-C-lightgrey.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

A comprehensive Linux kernel character device driver demonstrating mastery of advanced driver development concepts including multi-instance device management, sophisticated synchronization primitives, interrupt handling, and sysfs interface design.

## 🎯 Project Overview

This project implements a **GPIO-based serial communication device driver** that showcases the following advanced Linux kernel driver concepts:

### ✅ Core Features Implemented

- **🔧 Character Device Driver Fundamentals**
  - Dynamic device number allocation using `alloc_chrdev_region()`
  - Multiple device instances (3 devices) from single driver
  - Complete file operations structure with open, release, read, write, and ioctl handlers

- **⏳ Blocking I/O with Wait Queues**
  - `wait_queue_head_t` for blocking read operations
  - `wait_event_interruptible()` macro for process blocking
  - `wake_up_interruptible()` for process wakeup
  - Signal interruption handling with `-ERESTARTSYS`
  - Timeout support with `wait_event_interruptible_timeout()`

- **🔒 Kernel Synchronization Primitives**
  - **Spinlock** for interrupt-context synchronization
  - **Mutex** for blocking critical sections
  - Demonstrates spinlock vs mutex tradeoffs
  - Uses `spin_lock_irqsave()`/`spin_unlock_irqrestore()` for interrupt safety

- **🎛️ GPIO Control Implementation**
  - Bit-banging protocol for data transmission
  - State machine for data transmission management
  - Both synchronous (polled) and asynchronous (interrupt-driven) modes
  - Proper GPIO resource management and cleanup

- **📊 Sysfs Attribute Interface**
  - Read-only attributes for device status/statistics
  - Write-accessible attributes for device configuration
  - Uses `__ATTR()` macro for attribute definition
  - Implements `show()` and `store()` callback functions

## 📁 Project Structure

```
gpio-serial-chardev/
├── driver/
│   ├── gpio_serial.c          # Main driver implementation
│   ├── gpio_serial.h          # Header with common definitions
│   ├── gpio_serial_sysfs.c    # Sysfs interface implementation
│   └── Makefile               # Driver build configuration
├── userspace/
│   ├── test_app.c             # C test application
│   ├── test_recv.py           # Python receiver script
│   └── Makefile               # User-space build configuration
├── docs/
│   ├── DESIGN.md              # Architecture and design decisions
│   ├── SYNCHRONIZATION.md     # Sync primitives explanation
│   └── PROTOCOL.md            # GPIO bit-banging protocol spec
└── README.md                  # This file
```

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    User Space Applications                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   test_app.c    │  │  test_recv.py   │  │   Custom App │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                 Character Device Interface                  │
│  /dev/gpio_serial0, /dev/gpio_serial1, /dev/gpio_serial2   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Advanced Kernel Driver Layer                   │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  File Operations • Blocking I/O • Synchronization      │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  Protocol Engine • GPIO Control • Interrupt Handling   │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start Guide

### Prerequisites

```bash
# Install required packages (Ubuntu/Debian)
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) python3

# Install required packages (RHEL/CentOS)
sudo yum groupinstall "Development Tools"
sudo yum install kernel-devel-$(uname -r) python3
```

### Building the Driver

```bash
# Navigate to driver directory
cd gpio-serial-chardev/driver

# Build the kernel module
make

# Install and load the module
sudo make install

# Verify module is loaded
lsmod | grep gpio_serial
```

### Building User-Space Applications

```bash
# Navigate to userspace directory
cd ../userspace

# Build test applications
make

# Make Python script executable
make prepare

# Test compilation
make test
```

### Basic Usage

#### 1. Load the Driver

```bash
# Load the kernel module
sudo insmod gpio_serial.ko

# Check kernel messages
dmesg | tail -10
```

#### 2. Verify Device Creation

```bash
# Check device nodes
ls -la /dev/gpio_serial*

# Check sysfs interface
ls -la /sys/class/gpio_serial_class/
```

#### 3. Run Test Applications

```bash
# Run C test application
./test_app -w -n 10                    # Write test
./test_app -r -b                       # Blocking read test
./test_app -h                          # Show all options

# Run Python receiver
python3 test_recv.py --mode monitor    # Continuous monitoring
python3 test_recv.py --echo            # Enable echo mode
python3 test_recv.py --debug           # Debug output
```

## 💻 Usage Examples

### Character Device Interface

#### Opening and Reading from Device

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("/dev/gpio_serial0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    char buffer[256];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    
    if (bytes_read > 0) {
        printf("Received %zd bytes: %.*s\n", bytes_read, (int)bytes_read, buffer);
    }
    
    close(fd);
    return 0;
}
```

#### Writing to Device

```c
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = open("/dev/gpio_serial0", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    const char *message = "Hello GPIO Serial!";
    ssize_t bytes_written = write(fd, message, strlen(message));
    
    if (bytes_written > 0) {
        printf("Wrote %zd bytes\n", bytes_written);
    }
    
    close(fd);
    return 0;
}
```

#### Using IOCTL for Device Control

```c
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>

#define GPIO_SERIAL_RESET _IO('G', 0x01)
#define GPIO_SERIAL_GET_STATS _IOR('G', 0x02, unsigned long)

int main() {
    int fd = open("/dev/gpio_serial0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    // Reset device
    if (ioctl(fd, GPIO_SERIAL_RESET) < 0) {
        perror("IOCTL reset failed");
    }
    
    // Get statistics
    unsigned long stats;
    if (ioctl(fd, GPIO_SERIAL_GET_STATS, &stats) == 0) {
        printf("Device statistics: %lu\n", stats);
    }
    
    close(fd);
    return 0;
}
```

### Sysfs Interface

#### Reading Device Status

```bash
# Check device status
cat /sys/class/gpio_serial_class/gpio_serial0/status

# View statistics
cat /sys/class/gpio_serial_class/gpio_serial0/statistics
```

#### Configuring Device Parameters

```bash
# Configure GPIO pins
echo 18 > /sys/class/gpio_serial_class/gpio_serial0/tx_gpio
echo 19 > /sys/class/gpio_serial_class/gpio_serial0/rx_gpio

# Enable blocking mode
echo 1 > /sys/class/gpio_serial_class/gpio_serial0/blocking_mode

# Set timeout
echo 3000 > /sys/class/gpio_serial_class/gpio_serial0/timeout  # 3 seconds

# Enable echo mode
echo 1 > /sys/class/gpio_serial_class/gpio_serial0/echo_mode
```

### Python Interface

#### Basic Monitoring

```python
#!/usr/bin/env python3
import os
import time

# Open device
fd = os.open("/dev/gpio_serial0", os.O_RDONLY)

# Monitor for data
while True:
    try:
        data = os.read(fd, 1024)
        if data:
            print(f"Received: {data}")
        time.sleep(0.1)
    except KeyboardInterrupt:
        break

os.close(fd)
```

#### Configuration via Sysfs

```python
#!/usr/bin/env python3
import subprocess

def configure_device():
    # Enable echo mode
    with open("/sys/class/gpio_serial_class/gpio_serial0/echo_mode", "w") as f:
        f.write("1")
    
    # Set timeout
    with open("/sys/class/gpio_serial_class/gpio_serial0/timeout", "w") as f:
        f.write("5000")  # 5 seconds
    
    print("Device configured successfully")

configure_device()
```

## 🔧 Configuration Options

### Module Parameters

```bash
# Load with debug level
sudo insmod gpio_serial.ko debug_level=2

# Available debug levels:
# 0 = Quiet (no output)
# 1 = Normal (important messages)
# 2 = Verbose (all debug messages)
```

### Device Configuration

| Parameter | Description | Range | Default |
|-----------|-------------|-------|---------|
| `tx_gpio` | Transmit GPIO pin | 0-53 | 18, 20, 22 |
| `rx_gpio` | Receive GPIO pin | 0-53 | 19, 21, 23 |
| `blocking_mode` | Enable blocking I/O | 0/1 | 1 |
| `timeout` | Read timeout (jiffies) | 0-60*HZ | 5*HZ |
| `echo_mode` | Echo received data | 0/1 | 0 |

## 📊 Performance Characteristics

### Theoretical Performance

- **Baud Rate**: Up to 9600 bps (configurable)
- **Throughput**: ~960 bytes/second (8N1 format)
- **Latency**: < 10μs interrupt handling
- **Buffer Size**: 256 bytes circular buffer

### Real-World Considerations

- **GPIO Access Time**: 1-5μs per bit operation
- **Interrupt Latency**: System load dependent
- **Maximum Reliable Rate**: ~80% of theoretical due to overhead

## 🧪 Testing and Validation

### Built-in Tests

```bash
# Run comprehensive test suite
./test_app -w -r -b -t 10    # All tests, 10 second duration

# Test specific functionality
./test_app -w -n 100         # Write 100 bytes
./test_app -r -b             # Blocking read test
```

### Performance Benchmarking

```bash
# Run performance test
python3 test_recv.py --duration 30 --mode monitor

# Monitor device statistics
watch -n 1 'cat /sys/class/gpio_serial_class/gpio_serial0/statistics'
```

### Protocol Validation

```bash
# Test all byte values (0x00-0xFF)
./test_app -n 256

# Test error conditions
./test_app -d /dev/nonexistent  # Should fail gracefully
```

## 🐛 Debugging and Troubleshooting

### Common Issues

#### 1. Module Loading Fails

```bash
# Check kernel messages
dmesg | tail -20

# Verify kernel headers
ls /lib/modules/$(uname -r)/build

# Check GPIO availability
ls /sys/class/gpio/
```

#### 2. Permission Denied

```bash
# Check device permissions
ls -la /dev/gpio_serial*

# Fix permissions
sudo chmod 666 /dev/gpio_serial*

# Or add user to appropriate group
sudo usermod -a -G gpio $USER
```

#### 3. No Data Received

```bash
# Check device configuration
cat /sys/class/gpio_serial_class/gpio_serial0/status

# Verify GPIO setup
gpio readall  # If wiringPi is installed

# Check interrupt status
cat /proc/interrupts | grep gpio
```

### Debug Techniques

#### 1. Enable Verbose Debugging

```bash
# Load module with maximum debug level
sudo rmmod gpio_serial
sudo insmod gpio_serial.ko debug_level=2

# Monitor kernel messages
dmesg -w | grep GPIO_SERIAL
```

#### 2. Sysfs Inspection

```bash
# Check device status
cat /sys/class/gpio_serial_class/gpio_serial0/status

# Monitor statistics in real-time
watch -n 1 'cat /sys/class/gpio_serial_class/gpio_serial0/statistics'
```

#### 3. User-Space Debugging

```bash
# Run with strace to see system calls
strace ./test_app -w -n 10

# Run with valgrind for memory issues
valgrind --leak-check=full ./test_app
```

## 📚 Documentation

### Detailed Documentation

- **[DESIGN.md](docs/DESIGN.md)** - Architecture and design decisions
- **[SYNCHRONIZATION.md](docs/SYNCHRONIZATION.md)** - Kernel synchronization strategies
- **[PROTOCOL.md](docs/PROTOCOL.md)** - GPIO bit-banging protocol specification

### Code Documentation

```bash
# Generate documentation (requires doxygen)
cd driver
make docs

# View documentation
firefox docs/html/index.html
```

## 🎓 Learning Objectives

This project demonstrates mastery of:

### 1. Character Device Driver Architecture
- Dynamic device registration and management
- File operations implementation
- Device class and sysfs integration

### 2. Kernel Synchronization
- Spinlock vs mutex selection criteria
- Wait queue implementation for blocking I/O
- Interrupt-safe coding practices

### 3. Hardware Interface Programming
- GPIO control and configuration
- Interrupt handling and processing
- Bit-banging protocol implementation

### 4. Advanced Driver Concepts
- Multiple device instance management
- Sysfs attribute interface design
- Protocol state machine implementation

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

1. **Code Style**: Follow Linux kernel coding style
2. **Documentation**: Update relevant documentation
3. **Testing**: Add tests for new features
4. **Commit Messages**: Use descriptive commit messages

```bash
# Example contribution workflow
git checkout -b feature/new-feature
# Make changes
git commit -m "Add new feature: description"
git push origin feature/new-feature
# Create pull request
```

## 📄 License

This project is licensed under the **GNU General Public License v2.0** - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Linux Kernel Community** - For comprehensive documentation and examples
- **Kernel Newbies** - For excellent driver development tutorials
- **Embedded Systems Community** - For GPIO and hardware interfacing insights

## 📞 Support

For questions, issues, or discussions:

1. **GitHub Issues**: [Create an issue](https://github.com/your-repo/gpio-serial-chardev/issues)
2. **Documentation**: Check the [docs/](docs/) directory
3. **Kernel Mailing Lists**: For kernel development discussions

---

**Built with ❤️ for the Linux Kernel Development Community**

*This driver serves as both a practical implementation and an educational resource for understanding advanced Linux kernel character device driver development.*