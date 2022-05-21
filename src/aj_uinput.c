#include "aj_uinput.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>

#define AJ_UINPUT_PATH "/dev/uinput"

/* Object definition.
 */
 
struct aj_uinput {
  int fd;
  int have_key,have_abs;
  int did_setup;
  int did_commit;
  int dirty;
};

/* Delete.
 */
 
void aj_uinput_del(struct aj_uinput *uinput) {
  if (!uinput) return;
  if (uinput->fd>=0) close(uinput->fd);
  free(uinput);
}

/* New.
 */

struct aj_uinput *aj_uinput_new() {
  struct aj_uinput *uinput=calloc(1,sizeof(struct aj_uinput));
  if (!uinput) return 0;
  
  if ((uinput->fd=open(AJ_UINPUT_PATH,O_WRONLY))<0) {
    fprintf(stderr,"%s:open: %m\n",AJ_UINPUT_PATH);
    aj_uinput_del(uinput);
    return 0;
  }
  
  return uinput;
}

/* Setup.
 */
 
int aj_uinput_setup(
  struct aj_uinput *uinput,
  int vid,int pid,
  const char *name,int namec
) {
  if (!uinput) return -1;
  if (uinput->did_commit) return -1;
  struct uinput_setup setup={
    .id={
      .bustype=BUS_USB,
      .vendor=vid,
      .product=pid,
    },
  };
  
  // We'll sanitize the name a little just in case it comes from a dynamic source.
  if (!name) namec=0; else if (namec<0) { namec=0; while (name[namec]) namec++; }
  while (namec&&((unsigned char)name[namec-1]<=0x20)) namec--;
  int leadc=0;
  while ((leadc<namec)&&((unsigned char)name[leadc]<=0x20)) leadc++;
  if (leadc) {
    name+=leadc;
    namec-=leadc;
  }
  if (namec>=UINPUT_MAX_NAME_SIZE) namec=UINPUT_MAX_NAME_SIZE-1;
  memcpy(setup.name,name,namec);
  setup.name[namec]=0;
  int i=namec; while (i-->0) {
    if ((setup.name[i]<0x20)||(setup.name[i]>0x7e)) setup.name[i]='?';
  }
  
  if (ioctl(uinput->fd,UI_DEV_SETUP,&setup)<0) {
    fprintf(stderr,"UI_DEV_SETUP: %m\n");
    return -1;
  }
  uinput->did_setup=1;
  
  return 0;
}

/* Add capabilities.
 */

int aj_uinput_add_key(struct aj_uinput *uinput,int code) {
  if (!uinput) return -1;
  if (uinput->did_commit) return -1;
  if (!uinput->have_key) {
    if (ioctl(uinput->fd,UI_SET_EVBIT,EV_KEY)<0) {
      fprintf(stderr,"UI_SET_EVBIT(EV_KEY): %m\n");
      return -1;
    }
    uinput->have_key=1;
  }
  if (ioctl(uinput->fd,UI_SET_KEYBIT,code)<0) {
    fprintf(stderr,"UI_SET_KEYBIT: %m\n");
    return -1;
  }
  return 0;
}

int aj_uinput_add_abs(struct aj_uinput *uinput,int code,int lo,int hi) {
  if (!uinput) return -1;
  if (uinput->did_commit) return -1;
  if (!uinput->have_abs) {
    if (ioctl(uinput->fd,UI_SET_EVBIT,EV_ABS)<0) {
      fprintf(stderr,"UI_SET_EVBIT(EV_ABS): %m\n");
      return -1;
    }
    uinput->have_abs=1;
  }
  struct uinput_abs_setup setup={
    .code=code,
    .absinfo={
      .minimum=lo,
      .maximum=hi,
    },
  };
  // UI_ABS_SETUP implicitly sets the ABS bit too.
  if (ioctl(uinput->fd,UI_ABS_SETUP,&setup)<0) {
    fprintf(stderr,"UI_ABS_SETUP: %m\n");
    return -1;
  }
  return 0;
}

/* Commit.
 */

int aj_uinput_commit(struct aj_uinput *uinput) {
  if (!uinput) return -1;
  if (uinput->did_commit) return -1;
  if (!uinput->did_setup) {
    if (aj_uinput_setup(uinput,0,0,"Unknown Device",-1)<0) return -1;
  }
  if (ioctl(uinput->fd,UI_DEV_CREATE)<0) {
    fprintf(stderr,"UI_DEV_CREATE: %m\n");
    return -1;
  }
  uinput->did_commit=1;
  return 0;
}

/* Send events.
 */
 
int aj_uinput_event(struct aj_uinput *uinput,int type,int code,int value) {
  struct input_event event={
    .type=type,
    .code=code,
    .value=value,
  };
  if (write(uinput->fd,&event,sizeof(event))!=sizeof(event)) return -1;
  uinput->dirty=1;
  return 0;
}

int aj_uinput_sync(struct aj_uinput *uinput) {
  if (!uinput->dirty) return 0;
  if (aj_uinput_event(uinput,EV_SYN,SYN_REPORT,0)<0) return -1;
  uinput->dirty=0;
  return 0;
}
