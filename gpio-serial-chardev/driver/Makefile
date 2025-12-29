# Makefile for GPIO Serial Character Device Driver
#
# This Makefile builds the kernel module for the GPIO serial communication driver
# with support for multiple device instances, blocking I/O, and sysfs interface.

# Module name
obj-m := gpio_serial.o

# Additional object files (if any)
gpio_serial-objs := gpio_serial.o gpio_serial_sysfs.o

# Kernel build directory - auto-detect or specify manually
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Compiler flags
ccflags-y := -Wall -Werror -O2
ccflags-y += -DDEBUG=$(DEBUG_LEVEL)

# Default target
all: module

# Build the kernel module
module:
	@echo "Building GPIO Serial Character Device Driver..."
	@echo "Kernel build directory: $(KDIR)"
	@if [ ! -d "$(KDIR)" ]; then \
		echo "Error: Kernel headers not found at $(KDIR)"; \
		echo "Please install kernel headers or specify KDIR environment variable"; \
		exit 1; \
	fi
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.* .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions

# Install the module
install: module
	@echo "Installing GPIO Serial driver module..."
	sudo insmod gpio_serial.ko
	@echo "Module loaded. Check with: lsmod | grep gpio_serial"
	@echo "Device nodes created at: /dev/gpio_serial*"
	@echo "Sysfs interface at: /sys/class/gpio_serial_class/gpio_serial*"

# Uninstall the module
uninstall:
	@echo "Uninstalling GPIO Serial driver module..."
	-sudo rmmod gpio_serial
	@echo "Module unloaded"

# Test compilation (syntax check only)
syntax-check:
	@echo "Performing syntax check..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Show module information
info: module
	@echo "Module information:"
	modinfo gpio_serial.ko

# Debug build
debug: DEBUG_LEVEL=2
debug: ccflags-y += -DDEBUG=2 -g -O0
debug: module

# Verbose build
verbose: ccflags-y += -DVERBOSE=1
verbose: module

# Help target
help:
	@echo "GPIO Serial Character Device Driver Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the kernel module (default)"
	@echo "  module       - Build the kernel module"
	@echo "  clean        - Clean build artifacts"
	@echo "  install      - Install and load the module"
	@echo "  uninstall    - Uninstall the module"
	@echo "  syntax-check - Perform syntax check only"
	@echo "  info         - Show module information"
	@echo "  debug        - Build with debug symbols"
	@echo "  verbose      - Build with verbose output"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  KDIR         - Kernel build directory (default: /lib/modules/\$$(uname -r)/build)"
	@echo "  DEBUG_LEVEL  - Debug level (0=quiet, 1=normal, 2=verbose)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build module"
	@echo "  make KDIR=/usr/src/linux-headers-5.4.0  # Use specific kernel headers"
	@echo "  make DEBUG_LEVEL=2      # Build with verbose debugging"
	@echo "  make install            # Install and load module"

# Prevent make from deleting intermediate files
.PHONY: all module clean install uninstall syntax-check info debug verbose help

# Default goal
.DEFAULT_GOAL := all