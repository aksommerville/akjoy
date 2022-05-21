/* aj_device.h
 * Container for all the things related to one hardware device.
 * usbdev, uidev, ...other things?
 */
 
#ifndef AJ_DEVICE_H
#define AJ_DEVICE_H

struct aj_device;
struct aj_usbdev;
struct aj_hardware_config;

void aj_device_del(struct aj_device *device);

/* Configure this USB device according to config, create the uinput device,
 * and wrap it all in an aj_device container.
 * This sends the first input request.
 */
struct aj_device *aj_device_new(
  struct aj_usbdev *usbdev, // HANDOFF
  const struct aj_hardware_config *config,
  int busid,int devid
);

int aj_device_get_busid(const struct aj_device *device);
int aj_device_get_devid(const struct aj_device *device);
struct aj_usbdev *aj_device_get_usbdev(const struct aj_device *device);

/* Call on all devices when you receive SIGUSR1.
 * We'll poll the USB device, and if it returns a report, process it.
 */
int aj_device_poll(struct aj_device *device);

#endif
