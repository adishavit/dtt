// libusb device info queries -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly allow device info queries on valid
// device pointers. All these functions require arg0._nonnull != 0.
// Covers: libusb_get_bus_number, libusb_get_port_number,
//   libusb_get_device_address, libusb_get_device_speed,
//   libusb_get_port_numbers, libusb_get_parent, libusb_get_device

#include <libusb.h>
#include <stdint.h>

// Variant 1: query bus/port/address/speed from enumerated device
void variant_basic_info() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list = (libusb_device**)0x1;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    uint8_t bus = libusb_get_bus_number(list[0]);       // requires arg0._nonnull != 0
    uint8_t addr = libusb_get_device_address(list[0]);  // requires arg0._nonnull != 0
    int speed = libusb_get_device_speed(list[0]);       // requires arg0._nonnull != 0
    (void)bus;
    (void)addr;
    (void)speed;

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

// Variant 2: port numbers and parent
void variant_port_and_parent() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list = (libusb_device**)0x1;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    uint8_t ports[8];
    int depth = libusb_get_port_numbers(list[0], ports, sizeof(ports));
                                                        // requires arg0._nonnull != 0
    (void)depth;

    libusb_device *parent = libusb_get_parent(list[0]); // requires arg0._nonnull != 0
    (void)parent;

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

// Variant 3: libusb_get_device retrieves underlying device from handle
void variant_get_device(libusb_device *dev) {
    libusb_device_handle *handle = (libusb_device_handle*)0x1;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_device *underlying = libusb_get_device(handle);
                                                        // requires @handle_open != 0
    (void)underlying;

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 4: all info queries on a single device in sequence
void variant_all_queries() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list = (libusb_device**)0x1;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    libusb_device *dev = list[0];

    uint8_t bus = libusb_get_bus_number(dev);
    uint8_t addr = libusb_get_device_address(dev);
    int speed = libusb_get_device_speed(dev);
    uint8_t ports[8];
    int depth = libusb_get_port_numbers(dev, ports, sizeof(ports));
    libusb_device *parent = libusb_get_parent(dev);
    (void)bus; (void)addr; (void)speed; (void)depth; (void)parent;

    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

int main() {
    variant_basic_info();
    variant_port_and_parent();
    (void)variant_get_device;
    variant_all_queries();
    return 0;
}
