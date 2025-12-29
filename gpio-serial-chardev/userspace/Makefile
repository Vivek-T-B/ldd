# Makefile for GPIO Serial Character Device Driver User-space Applications
#
# This Makefile compiles the test applications that interact with the GPIO serial
# character device driver, demonstrating practical usage of the driver interface.

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = 
INCLUDES = 

# Python script permissions
PYTHON = python3

# Target executables
TARGETS = test_app

# Default target
all: $(TARGETS)

# Build test application
test_app: test_app.c
	@echo "Compiling test application..."
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)
	@echo "Built: $@"

# Clean build artifacts
clean:
	@echo "Cleaning user-space build artifacts..."
	rm -f $(TARGETS) *.o *.out
	rm -rf __pycache__ *.pyc
	@echo "Clean completed"

# Install applications
install: all
	@echo "Installing applications..."
	install -m 755 test_app /usr/local/bin/
	install -m 755 test_recv.py /usr/local/bin/gpio-serial-recv
	@echo "Applications installed to /usr/local/bin/"

# Uninstall applications
uninstall:
	@echo "Uninstalling applications..."
	rm -f /usr/local/bin/test_app
	rm -f /usr/local/bin/gpio-serial-recv
	@echo "Applications uninstalled"

# Make Python script executable
prepare: 
	@echo "Making Python script executable..."
	chmod +x test_recv.py
	@echo "Prepared for execution"

# Test compilation
test: all
	@echo "Testing compilation..."
	./test_app -h | head -5
	@echo "Compilation test completed"

# Debug build
debug: CFLAGS += -g -DDEBUG=1 -O0
debug: all

# Show application information
info: test_app
	@echo "Test Application Information:"
	@file test_app
	@echo ""
	@echo "Application usage:"
	@./test_app -h

# Check Python script syntax
python-check:
	@echo "Checking Python script syntax..."
	$(PYTHON) -m py_compile test_recv.py
	@echo "Python syntax check completed"

# Run basic functionality test
test-basic: all prepare
	@echo "Running basic functionality test..."
	@echo "Note: This test requires the driver to be loaded"
	./test_app -w -n 10

# Show help
help:
	@echo "GPIO Serial Character Device Driver - User-space Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all           - Build all applications (default)"
	@echo "  clean         - Clean build artifacts"
	@echo "  install       - Install applications to /usr/local/bin/"
	@echo "  uninstall     - Uninstall applications"
	@echo "  prepare       - Make Python script executable"
	@echo "  test          - Test compilation"
	@echo "  debug         - Build with debug symbols"
	@echo "  info          - Show application information"
	@echo "  python-check  - Check Python script syntax"
	@echo "  test-basic    - Run basic functionality test"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  CC            - C compiler (default: gcc)"
	@echo "  CFLAGS        - C compiler flags"
	@echo "  LDFLAGS       - Linker flags"
	@echo "  PYTHON        - Python interpreter (default: python3)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build applications"
	@echo "  make CC=clang          # Use Clang compiler"
	@echo "  make debug             # Build with debug symbols"
	@echo "  make install           # Install to system"

# Prevent make from deleting intermediate files
.PHONY: all clean install uninstall prepare test debug info python-check test-basic help

# Default goal
.DEFAULT_GOAL := all