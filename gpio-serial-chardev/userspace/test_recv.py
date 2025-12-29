#!/usr/bin/env python3
"""
GPIO Serial Character Device Driver Python Receiver Script

This script demonstrates receiving data from the GPIO serial character device
using Python, showing practical usage of the character device driver.

Features:
- Non-blocking and blocking reads
- Device configuration through sysfs
- Real-time data monitoring
- Statistics collection
- Error handling and recovery
"""

import os
import sys
import time
import signal
import argparse
from pathlib import Path

class GPIOSerialReceiver:
    """Python wrapper for GPIO serial character device"""
    
    def __init__(self, device_path="/dev/gpio_serial0", debug=False):
        self.device_path = device_path
        self.debug = debug
        self.fd = None
        self.keep_running = True
        
        # Signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, signum, frame):
        """Handle termination signals gracefully"""
        print(f"\nReceived signal {signum}, shutting down gracefully...")
        self.keep_running = False
        if self.fd:
            os.close(self.fd)
    
    def log(self, message):
        """Debug logging"""
        if self.debug:
            print(f"[DEBUG] {message}")
    
    def open_device(self, mode=os.O_RDONLY):
        """Open the character device"""
        try:
            self.log(f"Opening device: {self.device_path}")
            self.fd = os.open(self.device_path, mode)
            print(f"Device opened successfully (fd: {self.fd})")
            return True
        except OSError as e:
            print(f"Failed to open device {self.device_path}: {e}")
            print("Make sure the device driver is loaded: sudo insmod gpio_serial.ko")
            return False
    
    def close_device(self):
        """Close the character device"""
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
            self.log("Device closed")
    
    def read_data(self, buffer_size=1024, timeout=None):
        """Read data from the device with optional timeout"""
        try:
            if timeout is not None:
                # Use select for timeout
                import select
                ready, _, _ = select.select([self.fd], [], [], timeout)
                if not ready:
                    return None, "Timeout"
            
            data = os.read(self.fd, buffer_size)
            return data, None
        except OSError as e:
            return None, str(e)
    
    def read_nonblocking(self, buffer_size=1024):
        """Non-blocking read"""
        flags = fcntl.fcntl(self.fd, fcntl.F_GETFL)
        fcntl.fcntl(self.fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        
        try:
            data = os.read(self.fd, buffer_size)
            return data, None
        except OSError as e:
            if e.errno in (11, 35):  # EAGAIN, EWOULDBLOCK
                return None, "No data available"
            return None, str(e)
    
    def read_blocking(self, buffer_size=1024, timeout=5):
        """Blocking read with timeout"""
        flags = fcntl.fcntl(self.fd, fcntl.F_GETFL)
        fcntl.fcntl(self.fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)
        
        return self.read_data(buffer_size, timeout)
    
    def get_device_stats(self):
        """Get device statistics from sysfs"""
        device_name = os.path.basename(self.device_path)
        stats_path = f"/sys/class/gpio_serial_class/{device_name}/statistics"
        
        try:
            with open(stats_path, 'r') as f:
                stats = f.read()
            return stats
        except OSError as e:
            return f"Could not read statistics: {e}"
    
    def get_device_status(self):
        """Get device status from sysfs"""
        device_name = os.path.basename(self.device_path)
        status_path = f"/sys/class/gpio_serial_class/{device_name}/status"
        
        try:
            with open(status_path, 'r') as f:
                status = f.read()
            return status
        except OSError as e:
            return f"Could not read status: {e}"
    
    def configure_echo_mode(self, enable=True):
        """Enable or disable echo mode through sysfs"""
        device_name = os.path.basename(self.device_path)
        echo_path = f"/sys/class/gpio_serial_class/{device_name}/echo_mode"
        
        try:
            with open(echo_path, 'w') as f:
                f.write('1' if enable else '0')
            print(f"Echo mode {'enabled' if enable else 'disabled'}")
            return True
        except OSError as e:
            print(f"Failed to configure echo mode: {e}")
            return False
    
    def configure_blocking_mode(self, enable=True):
        """Enable or disable blocking mode through sysfs"""
        device_name = os.path.basename(self.device_path)
        blocking_path = f"/sys/class/gpio_serial_class/{device_name}/blocking_mode"
        
        try:
            with open(blocking_path, 'w') as f:
                f.write('1' if enable else '0')
            print(f"Blocking mode {'enabled' if enable else 'disabled'}")
            return True
        except OSError as e:
            print(f"Failed to configure blocking mode: {e}")
            return False
    
    def test_nonblocking_read(self, duration=10, interval=0.5):
        """Test non-blocking read mode"""
        print(f"Testing non-blocking read for {duration} seconds...")
        
        end_time = time.time() + duration
        read_count = 0
        
        while self.keep_running and time.time() < end_time:
            data, error = self.read_nonblocking(1024)
            
            if data:
                print(f"Received ({len(data)} bytes): {data}")
                read_count += 1
            elif error != "No data available":
                print(f"Error: {error}")
                break
            
            time.sleep(interval)
        
        print(f"Non-blocking test completed. Total reads: {read_count}")
        return True
    
    def test_blocking_read(self, duration=10):
        """Test blocking read mode"""
        print(f"Testing blocking read for {duration} seconds...")
        
        end_time = time.time() + duration
        read_count = 0
        
        while self.keep_running and time.time() < end_time:
            print(f"Waiting for data... (attempt {read_count + 1})")
            data, error = self.read_blocking(1024, timeout=2)
            
            if data:
                print(f"Received ({len(data)} bytes): {data}")
                read_count += 1
            elif error == "Timeout":
                print("Read timeout - no data received")
            elif error:
                print(f"Error: {error}")
                break
        
        print(f"Blocking test completed. Total reads: {read_count}")
        return True
    
    def continuous_monitoring(self, duration=30):
        """Continuous monitoring mode"""
        print(f"Starting continuous monitoring for {duration} seconds...")
        print("Press Ctrl+C to stop early")
        
        start_time = time.time()
        read_count = 0
        total_bytes = 0
        
        while self.keep_running and (time.time() - start_time) < duration:
            # Show periodic statistics
            if read_count > 0 and read_count % 10 == 0:
                print("\n--- Device Statistics ---")
                print(self.get_device_stats())
                print("-------------------------\n")
            
            data, error = self.read_data(1024, timeout=1.0)
            
            if data:
                print(f"[{time.strftime('%H:%M:%S')}] Received ({len(data)} bytes): {data}")
                read_count += 1
                total_bytes += len(data)
            elif error == "Timeout":
                continue  # Normal timeout, keep monitoring
            elif error:
                print(f"Error: {error}")
                break
        
        elapsed = time.time() - start_time
        print(f"\nMonitoring completed!")
        print(f"Duration: {elapsed:.1f} seconds")
        print(f"Total reads: {read_count}")
        print(f"Total bytes: {total_bytes}")
        print(f"Average rate: {total_bytes/elapsed:.1f} bytes/second")
        return True

def main():
    parser = argparse.ArgumentParser(
        description="GPIO Serial Character Device Driver Python Receiver",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                           # Start continuous monitoring
  %(prog)s --mode blocking           # Test blocking read mode
  %(prog)s --mode nonblocking        # Test non-blocking read mode
  %(prog)s --duration 60             # Monitor for 60 seconds
  %(prog)s --device /dev/gpio_serial1 # Use specific device
  %(prog)s --echo                    # Enable echo mode
  %(prog)s --debug                   # Enable debug output
        """)
    
    parser.add_argument("-d", "--device", default="/dev/gpio_serial0",
                       help="Device path (default: /dev/gpio_serial0)")
    parser.add_argument("-m", "--mode", choices=["monitor", "blocking", "nonblocking"],
                       default="monitor", help="Operation mode")
    parser.add_argument("--duration", type=int, default=30,
                       help="Duration in seconds for test modes")
    parser.add_argument("--echo", action="store_true",
                       help="Enable echo mode")
    parser.add_argument("--no-echo", dest="echo", action="store_false",
                       help="Disable echo mode")
    parser.add_argument("--blocking", action="store_true",
                       help="Enable blocking mode")
    parser.add_argument("--nonblocking", action="store_true",
                       help="Enable non-blocking mode")
    parser.add_argument("--debug", action="store_true",
                       help="Enable debug output")
    
    args = parser.parse_args()
    
    # Create receiver instance
    receiver = GPIOSerialReceiver(args.device, debug=args.debug)
    
    print("GPIO Serial Character Device Driver - Python Receiver")
    print(f"Device: {args.device}")
    print("=" * 50)
    
    # Open device
    if not receiver.open_device():
        return 1
    
    try:
        # Configure device based on arguments
        if args.echo:
            receiver.configure_echo_mode(True)
        
        if args.blocking:
            receiver.configure_blocking_mode(True)
        elif args.nonblocking:
            receiver.configure_blocking_mode(False)
        
        # Show device status
        print("\n--- Device Status ---")
        print(receiver.get_device_status())
        print("---------------------\n")
        
        # Run based on mode
        if args.mode == "monitor":
            receiver.continuous_monitoring(args.duration)
        elif args.mode == "blocking":
            receiver.test_blocking_read(args.duration)
        elif args.mode == "nonblocking":
            receiver.test_nonblocking_read(args.duration)
        
        # Show final statistics
        print("\n--- Final Statistics ---")
        print(receiver.get_device_stats())
        print("------------------------\n")
        
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    except Exception as e:
        print(f"Unexpected error: {e}")
        return 1
    finally:
        receiver.close_device()
    
    print("Receiver completed successfully!")
    return 0

if __name__ == "__main__":
    # Import fcntl only when needed
    import fcntl
    sys.exit(main())