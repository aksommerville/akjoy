#include "aj_usbdev.h"
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

/* Object definition.
 */
 
struct aj_usbdev {
  int fd;
  char *path;
  uint8_t *desc; // device, config, interface, endpoint, and class descriptors
  int descc;     // as reported by read() on a fresh device.
  struct usbdevfs_urb urb;
  void *rpt;
  int rpta;
  int rptc; // nonzero when a request is in flight; how much the user asked for
  int epin;
};

/* Delete.
 */

void aj_usbdev_del(struct aj_usbdev *usbdev) {
  if (!usbdev) return;
  
  if (usbdev->fd>=0) close(usbdev->fd);
  if (usbdev->path) free(usbdev->path);
  if (usbdev->desc) free(usbdev->desc);
  if (usbdev->rpt) free(usbdev->rpt);
  
  free(usbdev);
}

/* Read descriptors.
 * Just read and store them, parsing comes later.
 */
 
static int aj_usbdev_read_descriptors(struct aj_usbdev *usbdev) {
  int desca=0;
  char buf[256];
  while (1) {
    int bufc=read(usbdev->fd,buf,sizeof(buf));
    if (bufc<0) return -1;
    if (!bufc) break;
    if (usbdev->descc>desca-bufc) {
      int na=(usbdev->descc+bufc+256)&~255;
      void *nv=realloc(usbdev->desc,na);
      if (!nv) return -1;
      usbdev->desc=nv;
      desca=na;
    }
    memcpy(usbdev->desc+usbdev->descc,buf,bufc);
    usbdev->descc+=bufc;
    if (bufc<sizeof(buf)) break;
  }
  return 0;
}

/* New.
 */

struct aj_usbdev *aj_usbdev_new(const char *path) {
  if (!path||!path[0]) return 0;
  
  struct aj_usbdev *usbdev=calloc(1,sizeof(struct aj_usbdev));
  if (!usbdev) return 0;
  
  if ((usbdev->fd=open(path,O_RDWR))<0) {
    aj_usbdev_del(usbdev);
    return 0;
  }
  
  if (!(usbdev->path=strdup(path))) {
    aj_usbdev_del(usbdev);
    return 0;
  }
  
  if (aj_usbdev_read_descriptors(usbdev)<0) {
    aj_usbdev_del(usbdev);
    return 0;
  }
  
  return usbdev;
}

/* Trivial accessors.
 */
 
int aj_usbdev_get_descriptors(void *dstpp,const struct aj_usbdev *usbdev) {
  if (!usbdev) return 0;
  if (dstpp) *(void**)dstpp=usbdev->desc;
  return usbdev->descc;
}

const char *aj_usbdev_get_path(const struct aj_usbdev *usbdev) {
  if (!usbdev) return 0;
  return usbdev->path;
}

/* Set interface.
 */
 
int aj_usbdev_set_interface(struct aj_usbdev *usbdev,int intfid,int altid,int epid) {
  if (!usbdev||(usbdev->fd<0)) return -1;
  
  usbdev->epin=epid;
  
  int retryc=5;
  while (1) {
    if (ioctl(usbdev->fd,USBDEVFS_CLAIMINTERFACE,(unsigned int*)&intfid)>=0) break;
    if ((errno==ENOENT)&&(retryc-->0)) {
      // This happens for newly-connected devices. I'm guessing they're not fully configured on the kernel side yet?
      // 10 ms seems to do the trick.
      fprintf(stderr,"%s:USBDEVFS_CLAIMINTERFACE failed, will retry\n",usbdev->path);
      usleep(10000);
    } else {
      fprintf(stderr,"%s:USBDEVFS_CLAIMINTERFACE: %m\n",usbdev->path);
      return -1;
    }
  }
  
  /* XXX This times out on the N30, no idea why.
   * Let's just cross our fingers and hope that CLAIMINTERFACE is sufficient?
  struct usbdevfs_setinterface setintf={
    .interface=intfid,
    .altsetting=altid,
  };
  if (ioctl(usbdev->fd,USBDEVFS_SETINTERFACE,&setintf)<0) {
    fprintf(stderr,"%s:USBDEVFS_SETINTERFACE: %m\n",usbdev->path);
    return -1;
  }
  /**/
  
  //fprintf(stderr,"%s: Using endpoint 0x%02x on interface %d,%d\n",usbdev->path,usbdev->epin,intfid,altid);
  
  return 0;
}

/* Submit request.
 */
 
int aj_usbdev_submit_input_request(struct aj_usbdev *usbdev,int rptlen) {
  if (!usbdev||(usbdev->fd<0)) return -1;
  if (rptlen<1) return -1;
  if (usbdev->rptc) return -1; // request already in flight
  
  if (rptlen>usbdev->rpta) {
    void *na=malloc(rptlen);
    if (!na) return -1;
    if (usbdev->rpt) free(usbdev->rpt);
    usbdev->rpt=na;
    usbdev->rpta=rptlen;
  }
  
  memset(&usbdev->urb,0,sizeof(struct usbdevfs_urb));
  usbdev->urb.type=USBDEVFS_URB_TYPE_INTERRUPT;
  usbdev->urb.endpoint=usbdev->epin;
  usbdev->urb.buffer=usbdev->rpt;
  usbdev->urb.buffer_length=rptlen;
  usbdev->urb.signr=SIGUSR1;
  
  if (ioctl(usbdev->fd,USBDEVFS_SUBMITURB,&usbdev->urb)<0) {
    fprintf(stderr,"%s:USBDEVFS_SUBMITURB: %m\n",usbdev->path);
    return -1;
  }
  usbdev->rptc=rptlen;
  
  return 0;
}

/* Poll for response.
 */

int aj_usbdev_poll(void *dstpp,struct aj_usbdev *usbdev) {
  if (!usbdev||(usbdev->fd<0)) return 0;
  if (!usbdev->rptc) return 0;
  
  void *result=0;
  if (ioctl(usbdev->fd,USBDEVFS_REAPURBNDELAY,&result)<0) {
    if (errno==EAGAIN) return 0;
    if (errno==EINTR) return 0;
    fprintf(stderr,"%s:USBDEVFS_REAPURBNDELAY: %m\n",usbdev->path);
    return -1;
  }
  
  if (result!=&usbdev->urb) {
    fprintf(stderr,"%s: Unexpected result from USBDEVFS_REAPURBNDELAY %p, expected %p\n",usbdev->path,result,&usbdev->urb);
    usbdev->rptc=0;
    return -1;
  }
  
  int dstc=usbdev->rptc;
  usbdev->rptc=0;
  *(void**)dstpp=usbdev->rpt;
  return dstc;
}
