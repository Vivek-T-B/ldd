/*
 * GPIO Serial Character Device Driver Test Application
 * 
 * This application demonstrates interaction with the GPIO serial character device driver
 * through file operations, including:
 * - Opening device nodes
 * - Reading and writing data
 * - Using IOCTL for device control
 * - Configuring device parameters through sysfs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <signal.h>
#include <time.h>

#define DEVICE_PATH "/dev/gpio_serial0"
#define BUFFER_SIZE 256
#define TEST_DATA "Hello GPIO Serial Device!"

/* IOCTL commands (must match driver definitions) */
#define GPIO_SERIAL_RESET _IO('G', 0x01)
#define GPIO_SERIAL_GET_STATS _IOR('G', 0x02, unsigned long)
#define GPIO_SERIAL_CLEAR_STATS _IO('G', 0x03)

/* Statistics structure */
struct gpio_serial_stats {
    unsigned long bytes_received;
    unsigned long bytes_sent;
    unsigned long errors;
};

/* Global variables for signal handling */
volatile sig_atomic_t keep_running = 1;

/* Signal handler for graceful shutdown */
void signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down gracefully...\n", signum);
    keep_running = 0;
}

/* Print usage information */
void print_usage(const char *program_name)
{
    printf("GPIO Serial Character Device Driver Test Application\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -d <device>    Device path (default: %s)\n", DEVICE_PATH);
    printf("  -r             Read test (receive data)\n");
    printf("  -w             Write test (send data)\n");
    printf("  -b             Blocking read test\n");
    printf("  -t <seconds>   Test duration in seconds\n");
    printf("  -n <bytes>     Number of bytes to send/receive\n");
    printf("  -h             Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -w                    # Write test\n", program_name);
    printf("  %s -r -b                 # Blocking read test\n", program_name);
    printf("  %s -w -n 100             # Write 100 bytes\n", program_name);
    printf("  %s -d /dev/gpio_serial1  # Use device 1\n", program_name);
}

/* Test device IOCTL functionality */
int test_ioctl(int fd, const char *device_name)
{
    struct gpio_serial_stats stats;
    int ret;

    printf("Testing IOCTL functionality for %s...\n", device_name);

    /* Get statistics */
    ret = ioctl(fd, GPIO_SERIAL_GET_STATS, &stats);
    if (ret < 0) {
        perror("IOCTL GPIO_SERIAL_GET_STATS failed");
        return -1;
    }

    printf("Current statistics:\n");
    printf("  Bytes received: %lu\n", stats.bytes_received);
    printf("  Bytes sent: %lu\n", stats.bytes_sent);
    printf("  Errors: %lu\n", stats.errors);

    /* Clear statistics */
    ret = ioctl(fd, GPIO_SERIAL_CLEAR_STATS);
    if (ret < 0) {
        perror("IOCTL GPIO_SERIAL_CLEAR_STATS failed");
        return -1;
    }

    printf("Statistics cleared successfully\n");

    /* Reset device */
    ret = ioctl(fd, GPIO_SERIAL_RESET);
    if (ret < 0) {
        perror("IOCTL GPIO_SERIAL_RESET failed");
        return -1;
    }

    printf("Device reset successfully\n");

    return 0;
}

/* Test write functionality */
int test_write(int fd, int num_bytes)
{
    char write_buffer[BUFFER_SIZE];
    int ret, i;

    printf("Testing write functionality (%d bytes)...\n", num_bytes);

    /* Prepare test data */
    for (i = 0; i < num_bytes && i < BUFFER_SIZE - 1; i++) {
        write_buffer[i] = 'A' + (i % 26);
    }
    write_buffer[i] = '\0';

    printf("Writing data: \"%s\"\n", write_buffer);

    /* Write to device */
    ret = write(fd, write_buffer, strlen(write_buffer));
    if (ret < 0) {
        perror("Write failed");
        return -1;
    }

    printf("Successfully wrote %d bytes\n", ret);
    return 0;
}

/* Test read functionality (non-blocking) */
int test_read(int fd, int num_bytes)
{
    char read_buffer[BUFFER_SIZE];
    int ret;

    printf("Testing read functionality (%d bytes)...\n", num_bytes);

    /* Set non-blocking mode */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* Read from device */
    ret = read(fd, read_buffer, num_bytes < BUFFER_SIZE ? num_bytes : BUFFER_SIZE - 1);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("No data available (EAGAIN/EWOULDBLOCK)\n");
            return 0;  /* Not an error in non-blocking mode */
        }
        perror("Read failed");
        return -1;
    }

    read_buffer[ret] = '\0';
    printf("Read %d bytes: \"%s\"\n", ret, read_buffer);
    return 0;
}

/* Test blocking read functionality */
int test_blocking_read(int fd, int timeout_seconds)
{
    char read_buffer[BUFFER_SIZE];
    int ret;
    fd_set readfds;
    struct timeval timeout;

    printf("Testing blocking read functionality (timeout: %d seconds)...\n", timeout_seconds);

    /* Set blocking mode */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    /* Setup select for timeout */
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    printf("Waiting for data...\n");

    /* Wait for data with timeout */
    ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if (ret == 0) {
        printf("Read timeout after %d seconds\n", timeout_seconds);
        return 0;  /* Timeout is not an error */
    } else if (ret < 0) {
        perror("Select failed");
        return -1;
    }

    /* Data is available, read it */
    ret = read(fd, read_buffer, BUFFER_SIZE - 1);
    if (ret < 0) {
        perror("Read failed");
        return -1;
    }

    read_buffer[ret] = '\0';
    printf("Read %d bytes: \"%s\"\n", ret, read_buffer);
    return 0;
}

/* Test continuous read/write loop */
int test_continuous_mode(int fd, int duration_seconds)
{
    time_t start_time, current_time;
    int write_count = 0, read_count = 0;
    char buffer[64];
    int ret;

    printf("Testing continuous mode for %d seconds...\n", duration_seconds);

    start_time = time(NULL);

    while (keep_running && (difftime(time(NULL), start_time) < duration_seconds)) {
        /* Write test data */
        snprintf(buffer, sizeof(buffer), "Message %d", write_count++);
        ret = write(fd, buffer, strlen(buffer));
        if (ret > 0) {
            printf("Wrote: %s\n", buffer);
        }

        /* Small delay between operations */
        usleep(100000);  /* 100ms */

        /* Try to read any available data */
        ret = read(fd, buffer, sizeof(buffer) - 1);
        if (ret > 0) {
            buffer[ret] = '\0';
            printf("Read: %s\n", buffer);
            read_count++;
        }
    }

    printf("Continuous test completed. Write count: %d, Read count: %d\n", 
           write_count, read_count);
    return 0;
}

/* Test sysfs configuration */
int test_sysfs_config(const char *device_name)
{
    char sysfs_path[256];
    FILE *fp;
    int value;

    printf("Testing sysfs configuration for %s...\n", device_name);

    /* Test reading device status */
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/gpio_serial_class/%s/status", device_name);
    fp = fopen(sysfs_path, "r");
    if (fp) {
        printf("Device status:\n");
        while (fgets(buffer, sizeof(buffer), fp)) {
            printf("  %s", buffer);
        }
        fclose(fp);
    } else {
        printf("Could not open sysfs status file: %s\n", sysfs_path);
    }

    /* Test reading statistics */
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/gpio_serial_class/%s/statistics", device_name);
    fp = fopen(sysfs_path, "r");
    if (fp) {
        printf("Device statistics:\n");
        while (fgets(buffer, sizeof(buffer), fp)) {
            printf("  %s", buffer);
        }
        fclose(fp);
    } else {
        printf("Could not open sysfs statistics file: %s\n", sysfs_path);
    }

    /* Test enabling echo mode */
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/gpio_serial_class/%s/echo_mode", device_name);
    fp = fopen(sysfs_path, "w");
    if (fp) {
        fprintf(fp, "1\n");
        fclose(fp);
        printf("Echo mode enabled\n");
    } else {
        printf("Could not enable echo mode: %s\n", sysfs_path);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int fd = -1;
    int ret = 0;
    const char *device_path = DEVICE_PATH;
    int test_write_flag = 0;
    int test_read_flag = 0;
    int test_blocking_flag = 0;
    int test_duration = 5;
    int num_bytes = 32;
    int opt;

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Parse command line arguments */
    while ((opt = getopt(argc, argv, "d:rwbht:n:")) != -1) {
        switch (opt) {
        case 'd':
            device_path = optarg;
            break;
        case 'r':
            test_read_flag = 1;
            break;
        case 'w':
            test_write_flag = 1;
            break;
        case 'b':
            test_blocking_flag = 1;
            break;
        case 't':
            test_duration = atoi(optarg);
            break;
        case 'n':
            num_bytes = atoi(optarg);
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return 0;
        }
    }

    /* If no specific test was requested, run all tests */
    if (!test_write_flag && !test_read_flag && !test_blocking_flag) {
        test_write_flag = 1;
        test_read_flag = 1;
        test_blocking_flag = 1;
    }

    printf("GPIO Serial Character Device Driver Test Application\n");
    printf("Device: %s\n", device_path);
    printf("================================================\n");

    /* Open device */
    printf("Opening device %s...\n", device_path);
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure the device driver is loaded: sudo insmod gpio_serial.ko\n");
        return 1;
    }
    printf("Device opened successfully (fd: %d)\n", fd);

    /* Run tests */
    if (test_write_flag) {
        ret |= test_write(fd, num_bytes);
    }

    if (test_read_flag) {
        ret |= test_read(fd, num_bytes);
    }

    if (test_blocking_flag) {
        ret |= test_blocking_read(fd, test_duration);
    }

    /* Test IOCTL functionality */
    ret |= test_ioctl(fd, device_path);

    /* Test sysfs configuration */
    ret |= test_sysfs_config(basename(device_path));

    /* Test continuous mode if not interrupted */
    if (keep_running) {
        ret |= test_continuous_mode(fd, test_duration);
    }

    /* Close device */
    printf("Closing device...\n");
    close(fd);

    if (ret == 0) {
        printf("\nAll tests completed successfully!\n");
    } else {
        printf("\nSome tests failed. Check the output above.\n");
    }

    return ret;
}