#include "aj_app.h"

int main(int argc,char **argv) {
  struct aj_app *app=aj_app_new(argc,argv);
  if (!app) return 1;
  int status=0;
  while (1) {
    int err=aj_app_update(app);
    if (err<0) { status=1; break; }
    if (!err) { status=0; break; }
  }
  aj_app_del(app,status);
  return status;
}

#if 0 //XXX--------------------------------------------- earlier attempts:
#include "akjoy.h"
#include "aj_usbhost.h"
#include "aj_usbdev.h"
#include "aj_decode.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/poll.h>

/* Signal handler.
 */
 
static volatile int sigc=0;

static void rcvsig(int sigid) {
  switch (sigid) {
    case SIGINT: if (++sigc>=3) {
        fprintf(stderr,"Too many unprocessed signals.\n");
        exit(1);
      } break;
  }
}

/* Guess model from the USB handshake.
 */
 
static int akjoy_guess_model_reportless(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {
  
  // The original Xbox has a unique idProduct. If only it were always so easy...
  if ((usb->idVendor==0x045e)&&(usb->idProduct==0x0289)) return akjoy_uinput_configure_xbox(uinput,usb);
  
  // It also has a unique interface class and subclass.
  // Not sure how reliable this is, but I'd expect it to catch third-party Xbox controllers.
  if (
    (usb->bDeviceClass==0x00)&&
    (usb->bDeviceSubClass==0x00)&&
    (usb->bDeviceProtocol==0x00)&&
    (usb->sel.bInterfaceClass==0x58)&&
    (usb->sel.bInterfaceSubClass==0x42)&&
    (usb->sel.bInterfaceProtocol==0x00)
  ) return akjoy_uinput_configure_xbox(uinput,usb);
  
  // My Xbox 360 controller has bMaxPacketSize0 8, and the 8bitdos both have 64.
  // That doesn't sound reliable and I bet it's not, but the id-sounding things are all identical.
  // If this ever proves wrong, it should be safe to remove -- we'll do heuristic tests on the input reports too.
  if (usb->bMaxPacketSize0==8) return akjoy_uinput_configure_xbox360(uinput,usb);
  
  // N30 and SN30 are identical at the handshake. We'll have to wait for reports.
  return 0;
}

/* Unspecified model. Determine from report if possible.
 */
 
static int akjoy_update_auto(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {

  // Only the original Xbox uses the last 6 bytes of the report.
  if (memcmp(usb->rpt+0x0e,"\0\0\0\0\0\0",6)) return akjoy_uinput_configure_xbox(uinput,usb);
  
  // 0x06 and 0x08 are the low-order byte of lx and ly for 360.
  // Not used at all by either 8bitdo.
  // Original, they are X and Black -- likely to be zero at rest.
  if (usb->rpt[0x06]||usb->rpt[0x08]) return akjoy_uinput_configure_xbox360(uinput,usb);
  
  // 0x0a..0x0d are (rx,ry) for 360 and (la,ra,rx) for original. Unused by 8bitdo.
  // Hopefully we've caught the originals by now. We're calling it 360.
  if (memcmp(usb->rpt+0x0a,"\0\0\0\0",4)) return akjoy_uinput_configure_xbox360(uinput,usb);
  
  // It's one of the 8bitdos. They each have a few buttons that the other lacks.
  // Wait until one of them gets pressed.
  // For the N30, this means you must begin by pressing Home.
  if (usb->rpt[0x03]&0x04) return akjoy_uinput_configure_n30(uinput,usb);
  if (usb->rpt[0x03]&0xc3) return akjoy_uinput_configure_sn30(uinput,usb);

  // 8bitdo but we don't yet know which one. Keep waiting.
  return 0;
}

/* Report helpers.
 * Declare (n,p) as the new and previous reports.
 */
 
#define BIT(rptp,mask,tag) { \
  if (n[rptp]&mask) { \
    if (!(p[rptp]&mask)) { \
      if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,1)<0) return -1; \
    } \
  } else if (p[rptp]&mask) { \
    if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,0)<0) return -1; \
  } \
}

#define BYTE_ANALOGUE(rptp,tag) { \
  if (n[rptp]!=p[rptp]) { \
    if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,n[rptp])<0) return -1; \
  } \
}

#define BYTE_BINARY(rptp,tag,thresh) { \
  if (n[rptp]>=thresh) { \
    if (p[rptp]<thresh) { \
      if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,1)<0) return -1; \
    } \
  } else if (p[rptp]>=thresh) { \
    if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,0)<0) return -1; \
  } \
}

#define S16(rptp,tag,mlt) { \
  int16_t _nv=n[rptp]|(n[rptp+1]<<8); \
  int16_t _pv=p[rptp]|(p[rptp+1]<<8); \
  if (_nv!=_pv) { \
    if (mlt<0) { \
      if (_nv==-32768) _nv=32767; \
      else _nv=-_nv; \
    } \
    if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,_nv)<0) return -1; \
  } \
}

#define S8(rptp,tag,mlt) { \
  if (n[rptp]!=p[rptp]) { \
    int8_t _v=n[rptp]; \
    if (mlt<0) { \
      if (_v==-128) _v=127; \
      else _v=-_v; \
    } \
    if (akjoy_uinput_event(uinput,AKJOY_BUTTON_##tag,_v)<0) return -1; \
  } \
}

/* Original Xbox
 */
 
static int akjoy_update_xbox(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {
  
  const uint8_t *n=usb->rpt,*p=usb->pvrpt;
  const uint8_t thumbthresh=0x01;
  const uint8_t bwthresh=0x01;
  const uint8_t triggerthresh=0x01;
  
  // dpad and 2-state aux buttons
  if (n[0x02]!=p[0x02]) {
    switch (config->dpad) {
      case AKJOY_XFORM_DISABLE: break;
      case AKJOY_XFORM_DEFAULT:
      case AKJOY_XFORM_FLIP: { // flipping will be handled on the way out
          BIT(0x02,0x01,UP)
          BIT(0x02,0x02,DOWN)
          BIT(0x02,0x04,LEFT)
          BIT(0x02,0x08,RIGHT)
        } break;
    }
    switch (config->aux) {
      case AKJOY_XFORM_DISABLE: break;
      case AKJOY_XFORM_DEFAULT: {
          BIT(0x02,0x10,START)
          BIT(0x02,0x20,SELECT)
          BIT(0x02,0x40,LP)
          BIT(0x02,0x80,RP)
        } break;
    }
  }
  
  // analogue thumb buttons and triggers
  switch (config->thumb) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        BYTE_ANALOGUE(0x04,SOUTH) // A
        BYTE_ANALOGUE(0x05,EAST) // B
        BYTE_ANALOGUE(0x06,WEST) // X
        BYTE_ANALOGUE(0x07,NORTH) // Y
      } break;
    case AKJOY_XFORM_BINARY: {
        BYTE_BINARY(0x04,SOUTH,thumbthresh)
        BYTE_BINARY(0x05,EAST,thumbthresh)
        BYTE_BINARY(0x06,WEST,thumbthresh)
        BYTE_BINARY(0x07,NORTH,thumbthresh)
      } break;
  }
  switch (config->bw) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        BYTE_ANALOGUE(0x08,BLACK)
        BYTE_ANALOGUE(0x09,WHITE)
      } break;
    case AKJOY_XFORM_BINARY: {
        BYTE_BINARY(0x08,BLACK,bwthresh)
        BYTE_BINARY(0x09,WHITE,bwthresh)
      } break;
  }
  switch (config->trigger) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        BYTE_ANALOGUE(0x0a,L1)
        BYTE_ANALOGUE(0x0b,R1)
      } break;
    case AKJOY_XFORM_BINARY: {
        BYTE_BINARY(0x0a,L1,triggerthresh)
        BYTE_BINARY(0x0b,R1,triggerthresh)
      } break;
  }
  
  // analogue sticks
  switch (config->lstick) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S16(0x0c,LX,1)
        S16(0x0e,LY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S16(0x0c,LX,1)
        S16(0x0c,LY,-1)
      } break;
  }
  switch (config->rstick) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S16(0x10,RX,1)
        S16(0x12,RY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S16(0x10,RX,1)
        S16(0x12,RY,-1)
      } break;
  }
  
  return 0;
}

/* Xbox 360.
 */
 
static int akjoy_update_xbox360(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {
  const uint8_t *n=usb->rpt,*p=usb->pvrpt;
  const uint8_t triggerthresh=0x01;

  // Two-state buttons: dpad, thumbs, triggers, aux
  if (n[0x02]!=p[0x02]) {
    switch (config->dpad) {
      case AKJOY_XFORM_DISABLE: break;
      case AKJOY_XFORM_DEFAULT:
      case AKJOY_XFORM_FLIP: {
          BIT(0x02,0x01,UP)
          BIT(0x02,0x02,DOWN)
          BIT(0x02,0x04,LEFT)
          BIT(0x02,0x08,RIGHT)
        } break;
    }
    switch (config->aux) {
      case AKJOY_XFORM_DISABLE: break;
      case AKJOY_XFORM_DEFAULT: {
          BIT(0x02,0x10,START)
          BIT(0x02,0x20,SELECT)
          BIT(0x02,0x40,LP)
          BIT(0x02,0x80,RP)
        } break;
    }
  }
  if (n[0x03]!=p[0x03]) {
    if (config->trigger!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x01,L1)
      BIT(0x03,0x02,R1)
    }
    if (config->aux!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x04,HOME)
    }
    if (config->thumb!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x10,SOUTH)
      BIT(0x03,0x20,EAST)
      BIT(0x03,0x40,WEST)
      BIT(0x03,0x80,NORTH)
    }
  }
  
  // analogue triggers
  switch (config->trigger) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        BYTE_ANALOGUE(0x04,L2)
        BYTE_ANALOGUE(0x05,R2)
      } break;
    case AKJOY_XFORM_BINARY: {
        BYTE_BINARY(0x04,L2,triggerthresh)
        BYTE_BINARY(0x05,R2,triggerthresh)
      } break;
  }
  
  // analogue sticks
  switch (config->lstick) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S16(0x06,LX,1)
        S16(0x08,LY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S16(0x06,LX,1)
        S16(0x08,LY,-1)
      } break;
  }
  switch (config->rstick) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S16(0x0a,RX,1)
        S16(0x0c,RY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S16(0x0a,RX,1)
        S16(0x0c,RY,-1)
      } break;
  }

  return 0;
}

/* 8bitdo N30 (NES)
 */
 
static int akjoy_update_n30(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {
  const uint8_t *n=usb->rpt,*p=usb->pvrpt;
  
  // 2-state buttons (not dpad)
  if (n[0x02]!=p[0x02]) {
    if (config->aux!=AKJOY_XFORM_DISABLE) {
      BIT(0x02,0x10,START)
      BIT(0x02,0x20,SELECT)
    }
  }
  if (n[0x03]!=p[0x03]) {
    if (config->aux!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x04,HOME)
    }
    if (config->thumb!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x10,SOUTH) // a
      BIT(0x03,0x20,WEST) // b
    }
  }
  
  // dpad is a pair of s8 axes
  switch (config->dpad) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S8(0x07,DX,1)
        S8(0x09,DY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S8(0x07,DX,1)
        S8(0x09,DY,-1)
      } break;
  }
  
  return 0;
}

/* 8bitdo SN30 (SNES)
 */
 
static int akjoy_update_sn30(struct akjoy_config *config,struct akjoy_usb *usb,struct akjoy_uinput *uinput) {
  const uint8_t *n=usb->rpt,*p=usb->pvrpt;
  
  // 2-state buttons (not dpad)
  if (n[0x02]!=p[0x02]) {
    if (config->aux!=AKJOY_XFORM_DISABLE) {
      BIT(0x02,0x10,START)
      BIT(0x02,0x20,SELECT)
    }
  }
  if (n[0x03]!=p[0x03]) {
    if (config->trigger!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x01,L1)
      BIT(0x03,0x02,R1)
    }
    if (config->thumb!=AKJOY_XFORM_DISABLE) {
      BIT(0x03,0x10,SOUTH) // b
      BIT(0x03,0x20,EAST) // a
      BIT(0x03,0x40,WEST) // y
      BIT(0x03,0x80,NORTH) // x
    }
  }
  
  // dpad is a pair of s8 axes
  switch (config->dpad) {
    case AKJOY_XFORM_DISABLE: break;
    case AKJOY_XFORM_DEFAULT: {
        S8(0x07,DX,1)
        S8(0x09,DY,1)
      } break;
    case AKJOY_XFORM_FLIP: {
        S8(0x07,DX,1)
        S8(0x09,DY,-1)
      } break;
  }
  
  return 0;
}

/* usbhost callbacks.
 */
 
static int cb_usbhost_connect(int busid,int devid,const char *path,void *userdata) {
  fprintf(stderr,"%s %d:%d %s\n",__func__,busid,devid,path);
  return 0;
}

static int cb_usbhost_disconnect(int busid,int devid,void *userdata) {
  fprintf(stderr,"%s %03d:%03d\n",__func__,busid,devid);
  return 0;
}

/* Main entry point.
 */
 
int main(int argc,char **argv) {
  //freopen("/home/andy/proj/akjoy/report","w",stderr);

  signal(SIGINT,rcvsig);
  
  /* usbhost: works good
  struct aj_usbhost_delegate usbhost_delegate={
    .cb_connect=cb_usbhost_connect,
    .cb_disconnect=cb_usbhost_disconnect,
  };
  struct aj_usbhost *usbhost=aj_usbhost_new(&usbhost_delegate);
  if (!usbhost) return 1;
  
  while (!sigc) {
    struct pollfd pollfd={.fd=usbhost->fd,.events=POLLIN|POLLERR|POLLHUP};
    if (poll(&pollfd,1,1000)<0) {
      fprintf(stderr,"poll: %m\n");
      break;
    }
    if (!pollfd.revents) {
      //fprintf(stderr,"pass\n");
      continue;
    }
    //fprintf(stderr,"POLLED\n");
    if (aj_usbhost_read(usbhost)<0) {
      fprintf(stderr,"aj_usbhost_read failed\n");
      break;
    }
  }
  
  aj_usbhost_del(usbhost);
  /**/
  
  struct aj_usbdev *usbdev=aj_usbdev_new("/dev/bus/usb/001/008");
  if (!usbdev) {
    fprintf(stderr,"Failed to open device: %m\n");
    return 1;
  }
  
  const uint8_t *v=0;
  int c=aj_usbdev_get_descriptors(&v,usbdev);
  int i=0; for (;i<c;i++) fprintf(stderr," %02x",v[i]);
  fprintf(stderr,"\n");
  
  struct aj_usb_summary *summary=aj_usb_summarize(v,c);
  if (!summary) {
    fprintf(stderr,"Failed to digest device and config descriptors.\n");
    aj_usbdev_del(usbdev);
    return 1;
  }
  #define FLD(rpt,fld,fmt) fprintf(stderr,"%20s: "fmt"\n",#fld,rpt.fld);
  fprintf(stderr,"Device:\n");
  FLD(summary->device,bcdUsb,"%04x")
  FLD(summary->device,bDeviceClass,"%02x")
  FLD(summary->device,bDeviceSubClass,"%02x")
  FLD(summary->device,bDeviceProtocol,"%02x")
  FLD(summary->device,bMaxPacketSize0,"%d")
  FLD(summary->device,idVendor,"%04x")
  FLD(summary->device,idProduct,"%04x")
  FLD(summary->device,bcdDevice,"%04x")
  FLD(summary->device,iManufacturer,"%d")
  FLD(summary->device,iProduct,"%d")
  FLD(summary->device,iSerialNumber,"%d")
  FLD(summary->device,bNumConfigurations,"%d")
  fprintf(stderr,"Configuration:\n");
  FLD(summary->configuration,wTotalLength,"%d")
  FLD(summary->configuration,bNumInterfaces,"%d")
  FLD(summary->configuration,bConfigurationValue,"%d")
  FLD(summary->configuration,iConfiguration,"%d")
  FLD(summary->configuration,bmAttributes,"%02x")
  FLD(summary->configuration,bMaxPower,"%d")
  for (i=0;i<summary->optionc;i++) {
    struct aj_usb_option *option=summary->optionv+i;
    fprintf(stderr,"Option %d/%d:\n",i,summary->optionc);
    FLD(option->interface,bInterfaceNumber,"%d")
    FLD(option->interface,bAlternateSetting,"%d")
    FLD(option->interface,bNumEndpoints,"%d")
    FLD(option->interface,bInterfaceClass,"%02x")
    FLD(option->interface,bInterfaceSubClass,"%02x")
    FLD(option->interface,bInterfaceProtocol,"%02x")
    FLD(option->interface,iInterface,"%d")
    FLD(option->epin,bEndpointAddress,"%02x")
    FLD(option->epin,bmAttributes,"%02x")
    FLD(option->epin,wMaxPacketSize,"%d")
    FLD(option->epin,bInterval,"%d")
  }
  free(summary);
  
  aj_usbdev_del(usbdev);
  return 0;

#if 0//XXX play around a little with /sys/kernel/debug/usb/devices, see if a single-daemon strategy is feasible.
  struct akjoy_config config={0};
  int err=akjoy_config_argv(&config,argc,argv);
  if (err<0) return 1;
  if (!err) return 0;
  
  if (0) {
    char rpt[1024];
    int rptc=snprintf(rpt,sizeof(rpt),"device path: %s\n",config.devpath);
    int fd=open("/home/andy/proj/akjoy/report",O_WRONLY|O_CREAT|O_TRUNC,0666);
    if (fd>=0) {
      write(fd,rpt,rptc);
      close(fd);
    }
  }
  
  /*
  int pid=fork();
  if (pid<0) return 1;
  if (pid) {
    fprintf(stderr,"%s: Forked daemon pid %d\n",config.exename,pid);
    return 0;
  }
  setsid();
  //https://stackoverflow.com/questions/3095566/linux-daemonize
  // Says you should fork() again after setsid() to relinquish the role as session leader.
  if ((pid=fork())<0) return 1;
  if (pid) {
    fprintf(stderr,"%s: Forked even otherer daemon pid %d\n",config.exename,pid);
    return 0;
  }
  chdir("/");
  freopen("/dev/null","r",stdin);
  freopen("/dev/null","w",stdout);
  //freopen("/dev/null","w",stderr);
  */
  if (config.daemonize) {
    if (daemon(0,0)<0) return -1;
    unlink("/home/andy/proj/akjoy/daemonlog");
    freopen("/home/andy/proj/akjoy/daemonlog","w",stderr);
    setvbuf(stderr,0,_IONBF,0);
  } else {
    fprintf(stderr,"Running in foreground. (use --daemonize=1 if you don't want this)\n");
  }
  
  {
    time_t now=0;
    now=time(&now);
    fprintf(stderr,"Running daemon at time %d\n",(int)now);
    
    int uid=getuid();
    fprintf(stderr,"user id %d\n",uid);
    
  }
  
  /**/
  fprintf(stderr,"Config looks OK.\n");
  fprintf(stderr,"  devpath: %s\n",config.devpath);
  fprintf(stderr,"  hw_model: %d\n",config.hw_model);
  fprintf(stderr,"  name: %s\n",config.name);
  fprintf(stderr,"  vid: 0x%04x\n",config.vid);
  fprintf(stderr,"  pid: 0x%04x\n",config.pid);
  fprintf(stderr,"  dpad: %d\n",config.dpad);
  fprintf(stderr,"  thumb: %d\n",config.thumb);
  fprintf(stderr,"  bw: %d\n",config.bw);
  fprintf(stderr,"  aux: %d\n",config.aux);
  fprintf(stderr,"  trigger: %d\n",config.trigger);
  fprintf(stderr,"  lstick: %d\n",config.lstick);
  fprintf(stderr,"  rstick: %d\n",config.rstick);
  /**/
  
  struct akjoy_usb *usb=akjoy_usb_new(&config);
  if (!usb) {
    fprintf(stderr,"%d: akjoy_usb_new failed\n",getpid());
    akjoy_config_cleanup(&config);
    fflush(stderr);
    return 1;
  }

  /**/
  fprintf(stderr,"%s: Opened device.\n",config.devpath);
  fprintf(stderr,"  dev cls/sub/proto: %02x/%02x/%02x\n",usb->bDeviceClass,usb->bDeviceSubClass,usb->bDeviceProtocol);
  fprintf(stderr,"  intf cls/sub/proto: %02x/%02x/%02x\n",usb->sel.bInterfaceClass,usb->sel.bInterfaceSubClass,usb->sel.bInterfaceProtocol);
  fprintf(stderr,"  bMaxPacketSize0: %d\n",usb->bMaxPacketSize0);
  fprintf(stderr,"  vid/pid/version: %04x/%04x/%04x\n",usb->idVendor,usb->idProduct,usb->bcdDevice);
  fprintf(stderr,"  cfg/intf/alt/in/out: %02x/%02x/%02x/%02x/%02x\n",usb->sel.cfgid,usb->sel.intfid,usb->sel.altid,usb->sel.epin,usb->sel.epout);
  /**/
  
  struct akjoy_uinput *uinput=akjoy_uinput_new(&config,usb);
  if (!uinput) {
    akjoy_usb_del(usb);
    akjoy_config_cleanup(&config);
    fflush(stderr);
    return 1;
  }
  
  if (uinput->hw_model==AKJOY_MODEL_AUTO) {
    if (akjoy_guess_model_reportless(&config,usb,uinput)<0) {
      akjoy_usb_del(usb);
      akjoy_config_cleanup(&config);
      akjoy_uinput_del(uinput);
    fflush(stderr);
      return 1;
    }
    if (uinput->hw_model==AKJOY_MODEL_AUTO) {
      fprintf(stderr,"%s: Waiting for input reports to guess device type.\n",config.devpath);
    }
  }
  
  while (!sigc) {
    err=akjoy_usb_update(usb);
    if (err<0) {
      break;
    }
    if (err) {
      // xbox360 is good about only sending reports when changed, but the 8bitdos send a constant barrage.
      if (!memcmp(usb->rpt,usb->pvrpt,AKJOY_REPORT_LENGTH)) continue;
      err=0;
      switch (uinput->hw_model) {
        case AKJOY_MODEL_AUTO: err=akjoy_update_auto(&config,usb,uinput); break;
        case AKJOY_MODEL_XBOX: err=akjoy_update_xbox(&config,usb,uinput); break;
        case AKJOY_MODEL_XBOX360: err=akjoy_update_xbox360(&config,usb,uinput); break;
        case AKJOY_MODEL_N30: err=akjoy_update_n30(&config,usb,uinput); break;
        case AKJOY_MODEL_SN30: err=akjoy_update_sn30(&config,usb,uinput); break;
      }
      if (err<0) {
        fprintf(stderr,"%s: Abort due to error during report reception.\n",config.exename);
        break;
      }
      if (akjoy_uinput_sync(uinput)<0) {
        fprintf(stderr,"%s: Error synchronizing report.\n",config.exename);
        break;
      }
    }
  }
  
  fprintf(stderr,"%s: Normal exit.\n",config.exename);
  akjoy_uinput_del(uinput);
  akjoy_usb_del(usb);
  akjoy_config_cleanup(&config);
    fflush(stderr);
  return 0;
#endif
}
#endif
