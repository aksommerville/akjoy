#include "akjoy.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

/* Delete device.
 */
 
void akjoy_usb_del(struct akjoy_usb *usb) {
  if (!usb) return;
  if (usb->fd>=0) close(usb->fd);
  free(usb);
}

/* Fetch a string descriptor and convert from UTF-16 to UTF-8 into a new buffer (caller frees).
 */
 
static char *akjoy_usb_get_string(struct akjoy_usb *usb,int index) {
  int err;
  char src[256]={0};
  struct usbdevfs_ctrltransfer xfer={
    .bRequestType=0x80,
    .bRequest=6, // GET_DESCRIPTOR
    .wValue=0x0300|index, // type|index
    .wIndex=0x0409, // lang (en-us)
    .wLength=sizeof(src),
    .timeout=2000,
    .data=src,
  };
  if ((err=ioctl(usb->fd,USBDEVFS_CONTROL,&xfer))<0) {
    //fprintf(stderr,"USBDEVFS_CONTROL for string %d: %m\n",index);
    return 0;
  }
  int srcc=src[0];
  if (srcc>sizeof(src)) return 0;
  int dstc=(srcc>>1)-1; // don't actually reencode; just take the low byte from each
  char *dst=malloc(dstc+1);
  if (!dst) return 0;
  int dstp=0,srcp=2;
  for (;srcp<srcc;srcp+=2,dstp+=1) {
    char ch=src[srcp];
    if ((ch<0x20)||(ch>0x7e)) dst[dstp]='?';
    else dst[dstp]=ch;
  }
  dst[dstc]=0;
  return dst;
}

/* Compare (sel) to (seltmp) and copy from tmp if it's better.
 */
 
static void akjoy_usb_select(struct akjoy_usb *usb) {

  // Only interested if it has an input endpoint we like.
  if (!usb->seltmp.epin) return;
  
  // Refine a bit if they both have input.
  if (usb->sel.epin) {
    if (usb->sel.epout&&!usb->seltmp.epout) return;
    if (usb->seltmp.epout) {
      // Both have both input and output. Is there some other heuristic we could apply?
      return;
    }
  }
  
  // Take the new one.
  memcpy(&usb->sel,&usb->seltmp,sizeof(struct akjoy_usb_selection));
}

/* Receive descriptors.
 */
 
static inline uint16_t rd16(const uint8_t *src) {
  return src[0]|(src[1]<<8);
}
 
static int akjoy_usb_descriptor_device(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  
  if (srcc<18) return 0;
  uint16_t bcdUsb=rd16(src+2);
  usb->bDeviceClass=src[4];
  usb->bDeviceSubClass=src[5];
  usb->bDeviceProtocol=src[6];
  usb->bMaxPacketSize0=src[7];
  usb->idVendor=rd16(src+8);
  usb->idProduct=rd16(src+10);
  usb->bcdDevice=rd16(src+12);
  uint8_t iManufacturer=src[14];
  uint8_t iProduct=src[15];
  uint8_t iSerialNumber=src[16];
  uint8_t bNumConfigurations=src[17];
  
  return 0;
}
 
static int akjoy_usb_descriptor_configuration(struct akjoy_usb *usb,const uint8_t *src,int srcc) {

  akjoy_usb_select(usb);
  usb->seltmp.bInterfaceClass=0;
  usb->seltmp.bInterfaceSubClass=0;
  usb->seltmp.bInterfaceProtocol=0;
  usb->seltmp.cfgid=0;
  usb->seltmp.intfid=0;
  usb->seltmp.altid=0;
  usb->seltmp.epin=0;
  usb->seltmp.epout=0;
  
  if (srcc<9) return 0;
  uint16_t wTotalLength=rd16(src+2);
  uint8_t bNumInterfaces=src[4];
  uint8_t bConfigurationValue=src[5];
  uint8_t iConfiguration=src[6];
  uint8_t bmAttributes=src[7];
  uint8_t bMaxPower=src[8];
  
  usb->seltmp.cfgid=bConfigurationValue;
  
  return 0;
}
 
static int akjoy_usb_descriptor_interface(struct akjoy_usb *usb,const uint8_t *src,int srcc) {

  // Clear all but cfgid.
  akjoy_usb_select(usb);
  usb->seltmp.bInterfaceClass=0;
  usb->seltmp.bInterfaceSubClass=0;
  usb->seltmp.bInterfaceProtocol=0;
  usb->seltmp.intfid=0;
  usb->seltmp.altid=0;
  usb->seltmp.epin=0;
  usb->seltmp.epout=0;
  
  if (srcc<9) return 0;
  uint8_t bInterfaceNumber=src[2];
  uint8_t bAlternateSetting=src[3];
  uint8_t bNumEndpoints=src[4];
  usb->seltmp.bInterfaceClass=src[5];
  usb->seltmp.bInterfaceSubClass=src[6];
  usb->seltmp.bInterfaceProtocol=src[7];
  uint8_t iInterface=src[8];
  
  usb->seltmp.intfid=bInterfaceNumber;
  usb->seltmp.altid=bAlternateSetting;
  usb->seltmp.epin=0;
  usb->seltmp.epout=0;
  
  return 0;
}

static int akjoy_usb_descriptor_endpoint(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  if (srcc<7) return 0;
  uint8_t bEndpointAddress=src[2];
  uint8_t bmAttributes=src[3];
  uint16_t wMaxPacketSize=rd16(src+4);
  uint8_t bInterval=src[6];
  if (bEndpointAddress&0x80) { // input
    if (wMaxPacketSize>=AKJOY_REPORT_LENGTH) { // long enough for an input report
      if ((bmAttributes&3)==3) { // interrupt
        usb->seltmp.epin=bEndpointAddress;
      }
    }
  } else { // output
    if ((bmAttributes&3)==3) { // interrupt
      usb->seltmp.epout=bEndpointAddress;
    }
  }
  return 0;
}
 
static int akjoy_usb_descriptor_hid(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  return 0;
}
 
static int akjoy_usb_descriptor_hid_report(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  return 0;
}
 
static int akjoy_usb_descriptor_hid_physical(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  return 0;
}

/* Receive one descriptor from the handshake.
 */
 
static int akjoy_usb_descriptor(struct akjoy_usb *usb,const uint8_t *src,int srcc) {
  if (srcc<2) return 0;
  uint8_t bDescriptorType=src[1];
  switch (bDescriptorType) {
    case 0x01: return akjoy_usb_descriptor_device(usb,src,srcc);
    case 0x02: return akjoy_usb_descriptor_configuration(usb,src,srcc);
    case 0x03: return 0; // STRING, won't be included in the handshake
    case 0x04: return akjoy_usb_descriptor_interface(usb,src,srcc);
    case 0x05: return akjoy_usb_descriptor_endpoint(usb,src,srcc);
    case 0x06: return 0; // DEVICE_QUALIFIER
    case 0x07: return 0; // OTHER_SPEED_CONFIGURATION
    case 0x08: return 0; // INTERFACE_POWER
    case 0x21: return akjoy_usb_descriptor_hid(usb,src,srcc);
    case 0x22: return akjoy_usb_descriptor_hid_report(usb,src,srcc);
    case 0x23: return akjoy_usb_descriptor_hid_physical(usb,src,srcc);
  }
  return 0;
}

/* Handshake.
 */
 
static int akjoy_usb_handshake(struct akjoy_usb *usb) {

  uint8_t buf[1024]; // 360 returns 171 bytes, and the others are smaller. 1k is plenty.
  int bufc=read(usb->fd,buf,sizeof(buf));
  if (bufc<=0) {
    fprintf(stderr,"%s: Failed to read descriptors.\n",usb->config->devpath);
    return -1;
  }
  
  int bufp=0;
  while (bufp+2<=bufc) {
    uint8_t bLength=buf[bufp]; // including itself
    if ((bLength<2)||(bufp+bLength>bufc)) break;
    if (akjoy_usb_descriptor(usb,buf+bufp,bLength)<0) return -1;
    bufp+=bLength;
  }
  akjoy_usb_select(usb);
  
  if (!usb->sel.epin) {
    fprintf(stderr,"%s: Failed to locate a usable input endpoint.\n",usb->config->devpath);
    return -1;
  }
  
  /* per kernel.org, we should not call USBDEVFS_SETCONFIGURATION (config should already be set)
  int retryc=10;
  while (1) {
    uint32_t cfgid=usb->sel.cfgid;
    if (ioctl(usb->fd,USBDEVFS_SETCONFIGURATION,&cfgid)<0) {
      fprintf(stderr,"%s[%d]:USBDEVFS_SETCONFIGURATION: %m\n",usb->config->devpath,getpid());
      if (!retryc--) {
        fprintf(stderr,"give up\n");
        return -1;
      }
      fprintf(stderr,"will retry...\n");
      usleep(1000000);
    } else {
      fprintf(stderr,"USBDEVFS_SETCONFIGURATION ok %d\n",getpid());
      break;
    }
  }
  /**/
  
  /* Times out on N30
  struct usbdevfs_setinterface setintf={
    .interface=usb->sel.intfid,
    .altsetting=usb->sel.altid,
  };
  if (ioctl(usb->fd,USBDEVFS_SETINTERFACE,&setintf)<0) {
    fprintf(stderr,"%s:USBDEVFS_SETINTERFACE: %m\n",usb->config->devpath);
    return -1;
  }
  /**/
  
  uint32_t intfid=usb->sel.intfid;
  if (ioctl(usb->fd,USBDEVFS_CLAIMINTERFACE,&intfid)<0) {
    fprintf(stderr,"%s:USBDEVFS_CLAIMINTERFACE: %m\n",usb->config->devpath);
    return -1;
  }
  
  return 0;
}

/* New.
 */

struct akjoy_usb *akjoy_usb_new(struct akjoy_config *config) {
  if (!config||!config->devpath) return 0;
  struct akjoy_usb *usb=calloc(1,sizeof(struct akjoy_usb));
  if (!usb) return 0;
  
  usb->config=config;
  
  if ((usb->fd=open(config->devpath,O_RDWR))<0) {
    fprintf(stderr,"%s:open: %m\n",config->devpath);
    akjoy_usb_del(usb);
    return 0;
  }
  
  if (akjoy_usb_handshake(usb)<0) {
    akjoy_usb_del(usb);
    return 0;
  }
  
  return usb;
}

/* Update.
 */
 
int akjoy_usb_update(struct akjoy_usb *usb) {
  if (!usb) return -1;
  if (usb->fd<0) return -1;
  if (!usb->sel.epin) return -1;
  
  memcpy(usb->pvrpt,usb->rpt,AKJOY_REPORT_LENGTH);
  
  struct usbdevfs_urb urb={
    .type=USBDEVFS_URB_TYPE_INTERRUPT,
    .endpoint=usb->sel.epin,
    .status=0,
    .flags=0,
    .buffer=usb->rpt,
    .buffer_length=AKJOY_REPORT_LENGTH,
    .actual_length=0,
    .start_frame=0,
    .error_count=0,
    .signr=0,
    .usercontext=0,
  };
  if (ioctl(usb->fd,USBDEVFS_SUBMITURB,&urb)<0) {
    fprintf(stderr,"%s:USBDEVFS_SUBMITURB: %m\n",usb->config->devpath);
    return -1;
  }
  
  void *result=0;
  if (ioctl(usb->fd,USBDEVFS_REAPURB,&result)<0) {
    fprintf(stderr,"%s:USBDEVFS_REAPURB: %m\n",usb->config->devpath);
    return -1;
  }
  
  if (result!=&urb) return -1;
  
  if (urb.actual_length!=AKJOY_REPORT_LENGTH) {
    // Not a big deal. xbox360 does send a few of these at connect.
    //fprintf(stderr,"%s: Invalid report length %d\n",usb->config->devpath,urb.actual_length);
    return 0;
  }
  
  return 1;
}
