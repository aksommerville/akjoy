#include "aj_usbhost.h"
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/inotify.h>

//#define AJ_USBHOST_PATH "/sys/kernel/debug/usb/devices"
#define AJ_USBHOST_ROOT "/dev/bus/usb/"

static int aj_usbhost_scan(struct aj_usbhost *usbhost);

/* Delete.
 */
 
static void aj_usbhost_device_cleanup(struct aj_usbhost *usbhost,struct aj_usbhost_device *device) {
  if (device->path) free(device->path);
}

static void aj_usbhost_bus_cleanup(struct aj_usbhost *usbhost,struct aj_usbhost_bus *bus) {
  if ((usbhost->fd>=0)&&(bus->wd>=0)) inotify_rm_watch(usbhost->fd,bus->wd);
  if (bus->path) free(bus->path);
  if (bus->devicev) {
    while (bus->devicec-->0) aj_usbhost_device_cleanup(usbhost,bus->devicev+bus->devicec);
    free(bus->devicev);
  }
}
 
void aj_usbhost_del(struct aj_usbhost *usbhost) {
  if (!usbhost) return;
  if (usbhost->fd>=0) close(usbhost->fd);
  if (usbhost->busv) {
    while (usbhost->busc-->0) aj_usbhost_bus_cleanup(usbhost,usbhost->busv+usbhost->busc);
    free(usbhost->busv);
  }
  free(usbhost);
}

/* Add a bus record.
 */
 
static int aj_usbhost_add_bus(struct aj_usbhost *usbhost,int busid,int wd,const char *path) {
  if (busid<0) return -1;
  if (wd<0) return -1;
  if (usbhost->busc>=usbhost->busa) {
    int na=usbhost->busa+8;
    if (na>INT_MAX/sizeof(struct aj_usbhost_bus)) return -1;
    void *nv=realloc(usbhost->busv,sizeof(struct aj_usbhost_bus)*na);
    if (!nv) return -1;
    usbhost->busv=nv;
    usbhost->busa=na;
  }
  struct aj_usbhost_bus *bus=usbhost->busv+usbhost->busc++;
  memset(bus,0,sizeof(struct aj_usbhost_bus));
  bus->busid=busid;
  bus->wd=wd;
  if (!(bus->path=strdup(path))) return -1;
  return 0;
}

/* Find existing bus record.
 */
 
static struct aj_usbhost_bus *aj_usbhost_find_bus_by_busid(const struct aj_usbhost *usbhost,int busid) {
  struct aj_usbhost_bus *bus=usbhost->busv;
  int i=usbhost->busc;
  for (;i-->0;bus++) {
    if (bus->busid==busid) return bus;
  }
  return 0;
}
 
static struct aj_usbhost_bus *aj_usbhost_find_bus_by_wd(const struct aj_usbhost *usbhost,int wd) {
  struct aj_usbhost_bus *bus=usbhost->busv;
  int i=usbhost->busc;
  for (;i-->0;bus++) {
    if (bus->wd==wd) return bus;
  }
  return 0;
}

/* Device list in bus.
 */
 
static int aj_usbhost_bus_search_devicev(const struct aj_usbhost_bus *bus,int devid) {
  struct aj_usbhost_device *device=bus->devicev;
  int i=bus->devicec;
  for (;i-->0;device++) {
    if (device->devid==devid) return i;
  }
  return -1;
}

static struct aj_usbhost_device *aj_usbhost_bus_add_device(
  struct aj_usbhost_bus *bus,
  int devid,
  const char *dir,
  const char *base,int basec
) {
  if (bus->devicec>=bus->devicea) {
    int na=bus->devicea+8;
    if (na>INT_MAX/sizeof(struct aj_usbhost_device)) return 0;
    void *nv=realloc(bus->devicev,sizeof(struct aj_usbhost_device)*na);
    if (!nv) return 0;
    bus->devicev=nv;
    bus->devicea=na;
  }
  struct aj_usbhost_device *device=bus->devicev+bus->devicec++;
  memset(device,0,sizeof(struct aj_usbhost_device));
  device->devid=devid;
  
  int dirc=0;
  if (dir) while (dir[dirc]) dirc++;
  if (!base) basec=0; else if (basec<0) { basec=0; while (base[basec]) basec++; }
  int pathc=dirc+1+basec;
  if (!(device->path=malloc(pathc+1))) return 0;
  memcpy(device->path,dir,dirc);
  device->path[dirc]='/';
  memcpy(device->path+dirc+1,base,basec);
  device->path[pathc]=0;
  
  return device;
}

/* Scan /dev/bus/usb and watch all of its children (bus directories).
 */
 
static int aj_usbhost_add_watches(struct aj_usbhost *usbhost) {

  char subpath[1024];
  const char *parent=AJ_USBHOST_ROOT;
  int parentc=sizeof(AJ_USBHOST_ROOT)-1;
  if (parentc>=sizeof(subpath)) return -1;
  memcpy(subpath,parent,parentc);
  
  if (inotify_add_watch(usbhost->fd,parent,IN_CREATE|IN_DELETE)<0) return -1;
  
  DIR *dir=opendir(parent);
  if (!dir) return -1;
  struct dirent *de;
  while (de=readdir(dir)) {
    if (de->d_name[0]=='.') continue;
    if (de->d_type!=DT_DIR) continue;
    
    const char *base=de->d_name;
    int basec=0,busid=0;
    while (base[basec]) {
      if ((base[basec]<'0')||(base[basec]>'9')) {
        busid=-1;
        break;
      }
      busid*=10;
      busid+=base[basec]-'0';
      basec++;
    }
    if (busid<0) continue;
    if (aj_usbhost_find_bus_by_busid(usbhost,busid)) continue;
    if (parentc>=sizeof(subpath)-basec) continue;
    memcpy(subpath+parentc,base,basec+1);
    
    int wd=inotify_add_watch(usbhost->fd,subpath,IN_CREATE|IN_DELETE);
    if (aj_usbhost_add_bus(usbhost,busid,wd,subpath)<0) {
      closedir(dir);
      return -1;
    }
  }
  closedir(dir);
  
  return 0;
}

/* New.
 */

struct aj_usbhost *aj_usbhost_new(const struct aj_usbhost_delegate *delegate) {
  struct aj_usbhost *usbhost=calloc(1,sizeof(struct aj_usbhost));
  if (!usbhost) return 0;
  
  if (delegate) usbhost->delegate=*delegate;
  
  if ((usbhost->fd=inotify_init())<0) {
    fprintf(stderr,"inotify_init: %m\n");
    aj_usbhost_del(usbhost);
    return 0;
  }
  
  if (aj_usbhost_add_watches(usbhost)<0) {
    aj_usbhost_del(usbhost);
    return 0;
  }
  
  if (aj_usbhost_scan(usbhost)<0) {
    aj_usbhost_del(usbhost);
    return 0;
  }
  
  return usbhost;
}

/* Device added.
 */
 
static int aj_usbhost_add_device(
  struct aj_usbhost *usbhost,
  struct aj_usbhost_bus *bus,
  int devid,
  const char *base,int basec
) {
  if (aj_usbhost_bus_search_devicev(bus,devid)>=0) return 0;
  struct aj_usbhost_device *device=aj_usbhost_bus_add_device(bus,devid,bus->path,base,basec);
  if (!device) return -1;
  if (usbhost->delegate.cb_connect) {
    if (usbhost->delegate.cb_connect(bus->busid,device->devid,device->path,usbhost->delegate.userdata)<0) return -1;
  }
  return 0;
}

/* Device removed.
 */
 
static int aj_usbhost_remove_device(
  struct aj_usbhost *usbhost,
  struct aj_usbhost_bus *bus,
  int devid,
  const char *base,int basec
) {
  int p=aj_usbhost_bus_search_devicev(bus,devid);
  if (p<0) return 0;
  struct aj_usbhost_device *device=bus->devicev+p;
  aj_usbhost_device_cleanup(usbhost,device);
  bus->devicec--;
  memmove(device,device+1,sizeof(struct aj_usbhost_device)*(bus->devicec-p));
  if (usbhost->delegate.cb_disconnect) {
    if (usbhost->delegate.cb_disconnect(bus->busid,devid,usbhost->delegate.userdata)<0) return -1;
  }
  return 0;
}

/* Read.
 */
 
int aj_usbhost_read(struct aj_usbhost *usbhost) {
  if (usbhost->fd<0) return -1;
  char buf[1024];
  int bufc=read(usbhost->fd,buf,sizeof(buf));
  if (bufc<=0) {
    fprintf(stderr,"%s: reading inotify: %m\n",__func__);
    close(usbhost->fd);
    usbhost->fd=-1;
    return -1;
  }
  int bufp=0;
  while (bufp<=bufc-sizeof(struct inotify_event)) {
    struct inotify_event *event=(struct inotify_event*)(buf+bufp);
    bufp+=sizeof(struct inotify_event);
    if ((event->len<0)||(bufp>bufc-event->len)) break;
    bufp+=event->len;
    
    const char *base=event->name;
    int basec=0;
    while ((basec<event->len)&&base[basec]) basec++;
    if (basec<1) continue;
    
    int devid=0,i=0;
    for (;i<basec;i++) {
      if ((base[i]<'0')||(base[i]>'9')) {
        devid=-1;
        break;
      }
      devid*=10;
      devid+=base[i]-'0';
    }
    if (devid<0) continue;
    
    struct aj_usbhost_bus *bus=aj_usbhost_find_bus_by_wd(usbhost,event->wd);
    if (!bus) continue;
    
    if (event->mask&IN_CREATE) {
      if (aj_usbhost_add_device(usbhost,bus,devid,base,basec)<0) return -1;
    } else if (event->mask&IN_DELETE) {
      if (aj_usbhost_remove_device(usbhost,bus,devid,base,basec)<0) return -1;
    }
  }
  return 0;
}

/* Scan known busses for initial device set.
 */
 
static int aj_usbhost_scan_bus(struct aj_usbhost *usbhost,struct aj_usbhost_bus *bus) {
  if (!bus->path) return -1;
  DIR *dir=opendir(bus->path);
  if (!dir) return -1;
  struct dirent *de;
  while (de=readdir(dir)) {
    if (de->d_name[0]=='.') continue;
    if (de->d_type!=DT_CHR) continue;
    
    const char *base=de->d_name;
    int basec=0,devid=0;
    while (base[basec]) {
      if ((base[basec]<'0')||(base[basec]>'9')) {
        devid=-1;
        break;
      }
      devid*=10;
      devid+=base[basec]-'0';
      basec++;
    }
    if (devid<0) continue;
    if (aj_usbhost_bus_search_devicev(bus,devid)>=0) continue;
  
    if (aj_usbhost_add_device(usbhost,bus,devid,base,basec)<0) {
      closedir(dir);
      return -1;
    }
  }
  closedir(dir);
  return 0;
}
 
static int aj_usbhost_scan(struct aj_usbhost *usbhost) {
  if (!usbhost) return -1;
  struct aj_usbhost_bus *bus=usbhost->busv;
  int i=usbhost->busc;
  for (;i-->0;bus++) {
    if (aj_usbhost_scan_bus(usbhost,bus)<0) return -1;
  }
  return 0;
}
