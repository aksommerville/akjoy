#include "aj_app.h"
#include "aj_usbhost.h"
#include "aj_usbdev.h"
#include "aj_decode.h"
#include "aj_device.h"
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/poll.h>

/* Object definition and singleton.
 */
 
struct aj_app {

  volatile int sigintc;
  volatile int sigusr1c;
  
  struct aj_usbhost *usbhost;
  struct aj_device **devicev;
  int devicec,devicea;
  
  struct pollfd *pollfdv;
  int pollfdc,pollfda;
  int poll_timeout_ms;
  
};

static volatile struct aj_app *aj_app=0;

/* Signals.
 */
 
static void aj_app_rcvsig(int sigid) {
  //fprintf(stderr,"%s %d %s\n",__func__,sigid,strsignal(sigid));
  if (!aj_app) return;
  switch (sigid) {
    case SIGINT: if (++(aj_app->sigintc)>=3) {
        fprintf(stderr,"Too many unprocessed SIGINT. Aborting.\n");
        exit(1);
      } break;
    case SIGUSR1: aj_app->sigusr1c++; break;
  }
}

/* Delete.
 */
 
void aj_app_del(struct aj_app *app,int status) {
  fprintf(stderr,"%s %d\n",__func__,status);
  if (app==aj_app) aj_app=0;
  if (!app) return;
  
  if (status) {
    fprintf(stderr,"akjoy: Terminating due to error.\n");
  } else {
    fprintf(stderr,"akjoy: Normal exit.\n");
  }
  
  aj_usbhost_del(app->usbhost);
  
  if (app->devicev) {
    while (app->devicec-->0) aj_device_del(app->devicev[app->devicec]);
    free(app->devicev);
  }
  
  free(app);
}

/* USB device connected.
 */
 
static int aj_app_cb_connect(int busid,int devid,const char *path,void *userdata) {
  struct aj_app *app=userdata;
  
  if (app->devicec>=app->devicea) {
    int na=app->devicea+8;
    if (na>INT_MAX/sizeof(void*)) return -1;
    void *nv=realloc(app->devicev,sizeof(void*)*na);
    if (!nv) return -1;
    app->devicev=nv;
    app->devicea=na;
  }
  
  struct aj_usbdev *usbdev=aj_usbdev_new(path);
  if (!usbdev) return 0;
  
  struct aj_hardware_config config={0};
  const void *desc=0;
  int descc=aj_usbdev_get_descriptors(&desc,usbdev);
  struct aj_usb_summary *summary=aj_usb_summarize(desc,descc);
  if (!summary) {
    aj_usbdev_del(usbdev);
    return 0;
  }
  aj_hardware_config_decide(&config,summary);
  free(summary);
  
  if (!config.hwmodel) {
    aj_usbdev_del(usbdev);
    return 0;
  }
  
  /*
  fprintf(stderr,"%s: Acquired hardware config:\n",path);
  fprintf(stderr,"   model: %s\n",aj_hwmodel_repr(config.hwmodel));
  fprintf(stderr,"   interface: %d\n",config.intfid);
  fprintf(stderr,"   alternate: %d\n",config.altid);
  fprintf(stderr,"   endpoint: 0x%02x\n",config.epin);
  /**/
  
  struct aj_device *device=aj_device_new(usbdev,&config,busid,devid); // HANDOFF usbdev
  if (!device) {
    fprintf(stderr,"%s: Failed to initialize device\n",path);
    aj_usbdev_del(usbdev);
    return 0;
  }
  
  app->devicev[app->devicec++]=device;
  
  fprintf(stderr,"%03d:%03d: Connected\n",busid,devid);
  
  return 0;
}

/* USB device disconnected.
 */

static int aj_app_cb_disconnect(int busid,int devid,void *userdata) {
  struct aj_app *app=userdata;
  int i=app->devicec;
  while (i-->0) {
    struct aj_device *device=app->devicev[i];
    if (busid!=aj_device_get_busid(device)) continue;
    if (devid!=aj_device_get_devid(device)) continue;
    aj_device_del(device);
    app->devicec--;
    memmove(app->devicev+i,app->devicev+i+1,sizeof(void*)*(app->devicec-i));
    // Keep going, in case we got the same device twice somehow. It's cheap to check.
  }
  return 0;
}

/* New.
 */

struct aj_app *aj_app_new(int argc,char **argv) {
  if (aj_app) return 0; // One at a time, please.
  struct aj_app *app=calloc(1,sizeof(struct aj_app));
  if (!app) return 0;
  aj_app=app;
  
  //fprintf(stderr,"%s: pid %d\n",__func__,getpid());
  
  app->poll_timeout_ms=1000;
  
  signal(SIGINT,aj_app_rcvsig);
  signal(SIGUSR1,aj_app_rcvsig);
  
  srand(time(0));
  
  // We get a SIGTERM somewhere between here and "acquired usbhost". WHY???
  //...I guess we don't need to daemonize when systemd launches us. How convenient!
  
  struct aj_usbhost_delegate usbhost_delegate={
    .userdata=app,
    .cb_connect=aj_app_cb_connect,
    .cb_disconnect=aj_app_cb_disconnect,
  };
  if (!(app->usbhost=aj_usbhost_new(&usbhost_delegate))) {
    fprintf(stderr,"failed to open usbhost\n");
    aj_app_del(app,1);
    return 0;
  }
  
  return app;
}

/* Check devices for received urbs, in response to SIGUSR1.
 */
 
static int aj_app_poll_devices(struct aj_app *app) {
  int i=app->devicec;
  while (i-->0) {
    struct aj_device *device=app->devicev[i];
    if (aj_device_poll(device)<0) {
      fprintf(stderr,"%03d:%03d: Removing device.\n",aj_device_get_busid(device),aj_device_get_devid(device));
      aj_device_del(device);
      app->devicec--;
      memmove(app->devicev+i,app->devicev+i+1,sizeof(void*)*(app->devicec-i));
    }
  }
  return 0;
}

/* Update one polled file.
 */
 
static int aj_app_update_fd(struct aj_app *app,int fd) {

  if (app->usbhost&&(fd==app->usbhost->fd)) {
    return aj_usbhost_read(app->usbhost);
  }
  
  return 0;
}

/* Rebuild poll list.
 */
 
static int aj_app_pollfdv_append(struct aj_app *app,int fd) {
  if (fd<0) return -1;
  if (app->pollfdc>=app->pollfda) {
    int na=app->pollfda+8;
    if (na>INT_MAX/sizeof(struct pollfd)) return -1;
    void *nv=realloc(app->pollfdv,sizeof(struct pollfd)*na);
    if (!nv) return -1;
    app->pollfdv=nv;
    app->pollfda=na;
  }
  struct pollfd *pollfd=app->pollfdv+app->pollfdc++;
  memset(pollfd,0,sizeof(struct pollfd));
  pollfd->fd=fd;
  pollfd->events=POLLIN|POLLERR|POLLHUP;
  return 0;
}
 
static int aj_app_rebuild_pollfdv(struct aj_app *app) {
  app->pollfdc=0;
  
  if (app->usbhost&&(app->usbhost->fd>=0)) {
    if (aj_app_pollfdv_append(app,app->usbhost->fd)<0) return -1;
  }
  
  //TODO control socket
  
  //TODO other pollable things?
  
  return 0;
}

/* Update.
 */

int aj_app_update(struct aj_app *app) {
  if (!app) return -1;
  if (app->sigintc) return 0;
  
  if (app->sigusr1c) {
    app->sigusr1c=0;
    if (aj_app_poll_devices(app)<0) return -1;
  }
  
  if (aj_app_rebuild_pollfdv(app)<0) return -1;
  if (app->pollfdc>0) {
    int err=poll(app->pollfdv,app->pollfdc,app->poll_timeout_ms);
    if (err<0) {
      if (errno==EINTR) return 1;
      fprintf(stderr,"poll: %m\n");
      return -1;
    }
    struct pollfd *pollfd=app->pollfdv;
    int i=app->pollfdc;
    for (;i-->0;pollfd++) {
      if (!pollfd->revents) continue;
      if (aj_app_update_fd(app,pollfd->fd)<0) return -1;
    }
  } else {
    //TODO weird that we have nothing pollable -- if we have no devices either, terminate
    usleep(app->poll_timeout_ms*1000);
  }
 
  return 1;
}
