/* aj_usbhost.h
 * Monitors the system's USB stack to notify of connections and disconnections.
 * https://www.kernel.org/doc/html/v4.12/driver-api/usb/usb.html#dev-bus-usb-bbb-ddd
 * ^ That article says we should be able to use /sys/kernel/debug/usb/devices, but I haven't seen it work.
 * Instead we rely on inotify against /dev/bus/usb, and it works great.
 *
 * We tell the client about devices that appear and disappear from the bus.
 * We don't examine the devices, we don't know anything about them beyond their path.
 */
 
#ifndef AJ_USBHOST_H
#define AJ_USBHOST_H

struct aj_usbhost_delegate {
  void *userdata;
  int (*cb_connect)(int busid,int devid,const char *path,void *userdata);
  int (*cb_disconnect)(int busid,int devid,void *userdata);
};

struct aj_usbhost {
  struct aj_usbhost_delegate delegate;
  int fd;
  struct aj_usbhost_bus {
    int busid;
    int wd;
    char *path;
    struct aj_usbhost_device {
      int devid;
      char *path;
    } *devicev;
    int devicec,devicea;
  } *busv;
  int busc,busa;
};

void aj_usbhost_del(struct aj_usbhost *usbhost);

// Your (cb_connect) will fire during this call for each device already connected.
struct aj_usbhost *aj_usbhost_new(const struct aj_usbhost_delegate *delegate);

// Call whenever (usbhost->fd) polls readable.
int aj_usbhost_read(struct aj_usbhost *usbhost);

#endif
