/* aj_usbdev.h
 * A single connection to usbdevfs, one device.
 * Only root can create these.
 */
 
#ifndef AJ_USBDEV_H
#define AJ_USBDEV_H

struct aj_usbdev;

void aj_usbdev_del(struct aj_usbdev *usbdev);

struct aj_usbdev *aj_usbdev_new(const char *path);

// Descriptors are read at new(), this just returns the dump we already read.
int aj_usbdev_get_descriptors(void *dstpp,const struct aj_usbdev *usbdev);

const char *aj_usbdev_get_path(const struct aj_usbdev *usbdev);

int aj_usbdev_set_interface(struct aj_usbdev *usbdev,int intfid,int altid,int epin);

// USBDEVFS_SUBMITURB: Must call this, then wait, for each input report.
int aj_usbdev_submit_input_request(struct aj_usbdev *usbdev,int rptlen);

/* Call after aj_usbdev_input_request() to check the status of that request.
 * Returns length and populated (*dstpp) if we reaped the urb.
 * The pointer we put in (*dstpp) belongs to us, and we will overwrite it later.
 * Use it only until the next request.
 */
int aj_usbdev_poll(void *dstpp,struct aj_usbdev *usbdev);

#endif
