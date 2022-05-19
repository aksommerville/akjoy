#include "akjoy.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>

//TODO The uinput interface has changed (some time between 2019 and 2022?). Look into struct uinput_setup.

#define AKJOY_UINPUT_PATH "/dev/uinput"

// Fields that don't have an obvious mapping. In case I change my mind:
#define AKJOY_BTN_LP    BTN_THUMBL
#define AKJOY_BTN_RP    BTN_THUMBR
#define AKJOY_BTN_BLACK BTN_C
#define AKJOY_BTN_WHITE BTN_Z
#define AKJOY_BTN_HOME  BTN_MODE
#define AKJOY_ABS_SOUTH ABS_MISC+2
#define AKJOY_ABS_WEST  ABS_MISC+3
#define AKJOY_ABS_EAST  ABS_MISC+4
#define AKJOY_ABS_NORTH ABS_MISC+5
#define AKJOY_ABS_BLACK ABS_THROTTLE
#define AKJOY_ABS_WHITE ABS_RUDDER
#define AKJOY_ABS_L1    ABS_Z
#define AKJOY_ABS_R1    ABS_RZ
#define AKJOY_ABS_L2    ABS_MISC+0
#define AKJOY_ABS_R2    ABS_MISC+1

/* Delete.
 */
 
void akjoy_uinput_del(struct akjoy_uinput *uinput) {
  if (!uinput) return;
  if (uinput->fd>=0) close(uinput->fd);
  free(uinput);
}

/* New.
 */

struct akjoy_uinput *akjoy_uinput_new(struct akjoy_config *config,struct akjoy_usb *usb) {
  struct akjoy_uinput *uinput=calloc(1,sizeof(struct akjoy_uinput));
  if (!uinput) return 0;
  uinput->fd=-1;
  uinput->config=config;
  uinput->hw_model=AKJOY_MODEL_AUTO;
  
  int err=0;
  switch (config->hw_model) {
    case AKJOY_MODEL_XBOX: err=akjoy_uinput_configure_xbox(uinput,usb); break;
    case AKJOY_MODEL_XBOX360: err=akjoy_uinput_configure_xbox360(uinput,usb); break;
    case AKJOY_MODEL_N30: err=akjoy_uinput_configure_n30(uinput,usb); break;
    case AKJOY_MODEL_SN30: err=akjoy_uinput_configure_sn30(uinput,usb); break;
  }
  if (err<0) {
    akjoy_uinput_del(uinput);
    return 0;
  }
  
  return uinput;
}

/* Open connection to uinput.
 * We also send the two EVBIT ioctls, since they will be the same for all devices.
 */
 
static int akjoy_uinput_open(struct akjoy_uinput *uinput) {
  if (!uinput||(uinput->fd>=0)) return -1;
  if ((uinput->fd=open(AKJOY_UINPUT_PATH,O_RDWR))<0) {
    fprintf(stderr,"%s:open: %m\n",AKJOY_UINPUT_PATH);
    return -1;
  }
  if (
    (ioctl(uinput->fd,UI_SET_EVBIT,EV_KEY)<0)||
    (ioctl(uinput->fd,UI_SET_EVBIT,EV_ABS)<0)
  ) {
    fprintf(stderr,"%s:UI_SET_EVBIT: %m\n",AKJOY_UINPUT_PATH);
    return -1;
  }
  return 0;
}

/* Write the uud and send the ioctl to bring the device online.
 */
 
static int akjoy_uinput_enable(struct akjoy_uinput *uinput,const struct uinput_user_dev *uud) {
  if (write(uinput->fd,uud,sizeof(struct uinput_user_dev))!=sizeof(struct uinput_user_dev)) {
    fprintf(stderr,"%s:write: %m\n",AKJOY_UINPUT_PATH);
    return -1;
  }
  if (ioctl(uinput->fd,UI_DEV_CREATE)<0) {
    fprintf(stderr,"%s:UI_DEV_CREATE: %m\n",AKJOY_UINPUT_PATH);
    return -1;
  }
  fprintf(stderr,
    "%s: Created device %04x:%04x '%s'\n",
    uinput->config->devpath,
    uud->id.vendor,uud->id.product,uud->name
  );
  return 0;
}

/* Copy device IDs and name to the uud.
 */
 
static void akjoy_uinput_set_ids(
  struct uinput_user_dev *uud,
  struct akjoy_uinput *uinput,
  struct akjoy_usb *usb,
  const char *defaultname
) {
  
  // Name.
  const char *src;
  if (uinput->config->name) src=uinput->config->name;
  else src=defaultname;
  int srcc=0;
  while (src[srcc]) srcc++;
  if (srcc>=sizeof(uud->name)) srcc=sizeof(uud->name)-1;
  memcpy(uud->name,src,srcc);
  uud->name[srcc]=0;
  
  // Vendor/Product. If unset, consult hw_model -- don't always trust USB.
  uud->id.bustype=BUS_USB;
  if (uinput->config->vid&&uinput->config->pid) {
    uud->id.vendor=uinput->config->vid;
    uud->id.product=uinput->config->pid;
  } else switch (uinput->hw_model) {
    // 2dc8 is 8bitdo, evidently. "fac0" and "16bb" I just made up.
    case AKJOY_MODEL_N30: uud->id.vendor=0x2dc8; uud->id.product=0xfac0; break;
    case AKJOY_MODEL_SN30: uud->id.vendor=0x2dc8; uud->id.product=0x16bb; break;
    default: uud->id.vendor=usb->idVendor; uud->id.product=usb->idProduct; break;
  }
}

/* Add one report element.
 */
 
static int akjoy_uinput_add_abs(struct akjoy_uinput *uinput,struct uinput_user_dev *uud,int code,int lo,int hi) {
  if ((code<0)||(code>ABS_MAX)) return -1;
  uud->absmin[code]=lo;
  uud->absmax[code]=hi;
  if (ioctl(uinput->fd,UI_SET_ABSBIT,code)<0) return -1;
  return 0;
}

static int akjoy_uinput_add_key(struct akjoy_uinput *uinput,int code) {
  if (ioctl(uinput->fd,UI_SET_KEYBIT,code)<0) return -1;
  return 0;
}

/* Configure Xbox.
 */
 
int akjoy_uinput_configure_xbox(struct akjoy_uinput *uinput,struct akjoy_usb *usb) {
  if (akjoy_uinput_open(uinput)<0) return -1;
  struct uinput_user_dev uud={0};
  uinput->hw_model=AKJOY_MODEL_XBOX;
  akjoy_uinput_set_ids(&uud,uinput,usb,"Xbox");
  
  if (uinput->config->dpad!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0X,-1,1)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0Y,-1,1)<0) return -1;
  }
  
  if (uinput->config->aux!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_START)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_SELECT)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_LP)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_RP)<0) return -1;
  }
  
  switch (uinput->config->thumb) {
    case AKJOY_XFORM_DEFAULT: {
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_SOUTH,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_WEST,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_EAST,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_NORTH,0,255)<0) return -1;
        uinput->analogue_thumbs=1;
      } break;
    case AKJOY_XFORM_BINARY: {
        if (akjoy_uinput_add_key(uinput,BTN_SOUTH)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_WEST)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_EAST)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_NORTH)<0) return -1;
      } break;
  }
  
  switch (uinput->config->bw) {
    case AKJOY_XFORM_DEFAULT: {
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_BLACK,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_WHITE,0,255)<0) return -1;
        uinput->analogue_bw=1;
      } break;
    case AKJOY_XFORM_BINARY: {
        if (akjoy_uinput_add_key(uinput,AKJOY_BTN_BLACK)<0) return -1;
        if (akjoy_uinput_add_key(uinput,AKJOY_BTN_WHITE)<0) return -1;
      } break;
  }
  
  switch (uinput->config->trigger) {
    case AKJOY_XFORM_DEFAULT: {
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_L1,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_R1,0,255)<0) return -1;
        uinput->analogue_triggers=1;
      } break;
    case AKJOY_XFORM_BINARY: {
        if (akjoy_uinput_add_key(uinput,BTN_TL)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_TR)<0) return -1;
      } break;
  }
  
  if (uinput->config->lstick!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_X,-32768,32767)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_Y,-32768,32767)<0) return -1;
  }
  if (uinput->config->rstick!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_RX,-32768,32767)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_RY,-32768,32767)<0) return -1;
  }
  
  return akjoy_uinput_enable(uinput,&uud);
}

/* Configure Xbox 360.
 */

int akjoy_uinput_configure_xbox360(struct akjoy_uinput *uinput,struct akjoy_usb *usb) {
  if (akjoy_uinput_open(uinput)<0) return -1;
  struct uinput_user_dev uud={0};
  uinput->hw_model=AKJOY_MODEL_XBOX360;
  akjoy_uinput_set_ids(&uud,uinput,usb,"Xbox 360");
  
  if (uinput->config->dpad!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0X,-1,1)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0Y,-1,1)<0) return -1;
  }
  
  if (uinput->config->aux!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_START)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_SELECT)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_HOME)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_LP)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_RP)<0) return -1;
  }
  
  if (uinput->config->thumb!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_SOUTH)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_WEST)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_EAST)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_NORTH)<0) return -1;
  }
  
  switch (uinput->config->trigger) {
    case AKJOY_XFORM_DEFAULT: {
        if (akjoy_uinput_add_key(uinput,BTN_TL)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_TR)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_L2,0,255)<0) return -1;
        if (akjoy_uinput_add_abs(uinput,&uud,AKJOY_ABS_R2,0,255)<0) return -1;
        uinput->analogue_triggers=1;
      } break;
    case AKJOY_XFORM_BINARY: {
        if (akjoy_uinput_add_key(uinput,BTN_TL)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_TR)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_TL2)<0) return -1;
        if (akjoy_uinput_add_key(uinput,BTN_TR2)<0) return -1;
      } break;
  }
  
  if (uinput->config->lstick!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_X,-32768,32767)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_Y,-32768,32767)<0) return -1;
  }
  if (uinput->config->rstick!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_RX,-32768,32767)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_RY,-32768,32767)<0) return -1;
  }
  
  return akjoy_uinput_enable(uinput,&uud);
}

/* Configure N30.
 */

int akjoy_uinput_configure_n30(struct akjoy_uinput *uinput,struct akjoy_usb *usb) {
  if (akjoy_uinput_open(uinput)<0) return -1;
  struct uinput_user_dev uud={0};
  uinput->hw_model=AKJOY_MODEL_N30;
  akjoy_uinput_set_ids(&uud,uinput,usb,"8bitdo NES");
  
  if (uinput->config->dpad!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0X,-1,1)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0Y,-1,1)<0) return -1;
  }
  
  if (uinput->config->aux!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_START)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_SELECT)<0) return -1;
    if (akjoy_uinput_add_key(uinput,AKJOY_BTN_HOME)<0) return -1;
  }
  
  if (uinput->config->thumb!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_SOUTH)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_WEST)<0) return -1;
  }
  
  return akjoy_uinput_enable(uinput,&uud);
}

/* Configure SN30.
 */

int akjoy_uinput_configure_sn30(struct akjoy_uinput *uinput,struct akjoy_usb *usb) {
  if (akjoy_uinput_open(uinput)<0) return -1;
  struct uinput_user_dev uud={0};
  uinput->hw_model=AKJOY_MODEL_SN30;
  akjoy_uinput_set_ids(&uud,uinput,usb,"8bitdo SNES");
  
  if (uinput->config->dpad!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0X,-1,1)<0) return -1;
    if (akjoy_uinput_add_abs(uinput,&uud,ABS_HAT0Y,-1,1)<0) return -1;
  }
  
  if (uinput->config->aux!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_START)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_SELECT)<0) return -1;
  }
  
  if (uinput->config->thumb!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_SOUTH)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_WEST)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_EAST)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_NORTH)<0) return -1;
  }
  
  if (uinput->config->trigger!=AKJOY_XFORM_DISABLE) {
    if (akjoy_uinput_add_key(uinput,BTN_TL)<0) return -1;
    if (akjoy_uinput_add_key(uinput,BTN_TR)<0) return -1;
  }
  
  return akjoy_uinput_enable(uinput,&uud);
}

/* Send event, transport.
 */
 
static int akjoy_uinput_send(struct akjoy_uinput *uinput,uint8_t type,uint16_t code,int value) {
  struct input_event event={.type=type,.code=code,.value=value};
  if (write(uinput->fd,&event,sizeof(event))!=sizeof(event)) return -1;
  uinput->dirty=1;
  return 0;
}

/* Send event.
 */
 
int akjoy_uinput_event(struct akjoy_uinput *uinput,int btnid,int value) {
  //fprintf(stderr,"%s %10s = %d\n",__func__,akjoy_button_repr(btnid),value);
  if (!uinput||(uinput->fd<0)) return 0;
  switch (btnid) {

    // Dpad. Xbox and Xbox360 only. We combine into ABS_HAT0X,ABS_HAT0Y.
    #define BTN2DPAD(btntag,trackfld,abscode,dir) \
      case AKJOY_BUTTON_##btntag: if (value) { \
          if (uinput->trackfld==(dir)) return 0; \
          if (akjoy_uinput_send(uinput,EV_ABS,abscode,dir)<0) return -1; \
          uinput->trackfld=dir; \
        } else { \
          if (uinput->trackfld!=(dir)) return 0; \
          if (akjoy_uinput_send(uinput,EV_ABS,abscode,0)<0) return -1; \
          uinput->trackfld=0; \
        } break;
    BTN2DPAD(LEFT,dx,ABS_HAT0X,-1)
    BTN2DPAD(RIGHT,dx,ABS_HAT0X,1)
    BTN2DPAD(UP,dy,ABS_HAT0Y,(uinput->config->dpad==AKJOY_XFORM_FLIP)?-1:1)
    BTN2DPAD(DOWN,dy,ABS_HAT0Y,(uinput->config->dpad==AKJOY_XFORM_FLIP)?1:-1)
    // Arguably, since we're making these axes up, we could put them in the "correct" orientation to begin with.
    // My opinion is that would only add to the confusion.
    #undef BTN2DPAD
      
    // Thumb buttons. Analogue or binary.
    case AKJOY_BUTTON_SOUTH: if (uinput->analogue_thumbs) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_SOUTH,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_SOUTH,value);
      } break;
    case AKJOY_BUTTON_WEST: if (uinput->analogue_thumbs) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_WEST,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_WEST,value);
      } break;
    case AKJOY_BUTTON_EAST: if (uinput->analogue_thumbs) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_EAST,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_EAST,value);
      } break;
    case AKJOY_BUTTON_NORTH: if (uinput->analogue_thumbs) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_NORTH,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_NORTH,value);
      } break;
      
    // Black and white. Analogue or binary.
    case AKJOY_BUTTON_BLACK: if (uinput->analogue_bw) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_BLACK,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,AKJOY_BTN_BLACK,value);
      } break;
    case AKJOY_BUTTON_WHITE: if (uinput->analogue_bw) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_WHITE,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,AKJOY_BTN_WHITE,value);
      } break;
      
    // Triggers. Analogue or binary. L1 and R1 for the 360 are binary no matter what.
    case AKJOY_BUTTON_L1: if (uinput->hw_model==AKJOY_MODEL_XBOX360) {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TL,value);
      } else if (uinput->analogue_triggers) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_L1,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TL,value);
      } break;
    case AKJOY_BUTTON_R1: if (uinput->hw_model==AKJOY_MODEL_XBOX360) {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TL,value);
      } else if (uinput->analogue_triggers) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_R1,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TR,value);
      } break;
    case AKJOY_BUTTON_L2: if (uinput->analogue_triggers) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_L2,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TL2,value);
      } break;
    case AKJOY_BUTTON_R2: if (uinput->analogue_triggers) {
        return akjoy_uinput_send(uinput,EV_ABS,AKJOY_ABS_R2,value);
      } else {
        return akjoy_uinput_send(uinput,EV_KEY,BTN_TR2,value);
      } break;
      
    // Aux buttons: Start, Select, LP, RP, Home. All 2-state.
    case AKJOY_BUTTON_LP: return akjoy_uinput_send(uinput,EV_KEY,AKJOY_BTN_LP,value);
    case AKJOY_BUTTON_RP: return akjoy_uinput_send(uinput,EV_KEY,AKJOY_BTN_RP,value);
    case AKJOY_BUTTON_START: return akjoy_uinput_send(uinput,EV_KEY,BTN_START,value);
    case AKJOY_BUTTON_SELECT: return akjoy_uinput_send(uinput,EV_KEY,BTN_SELECT,value);
    case AKJOY_BUTTON_HOME: return akjoy_uinput_send(uinput,EV_KEY,AKJOY_BTN_HOME,value);
    
    // Analogue thumbsticks.
    case AKJOY_BUTTON_LX: return akjoy_uinput_send(uinput,EV_ABS,ABS_X,value);
    case AKJOY_BUTTON_LY: return akjoy_uinput_send(uinput,EV_ABS,ABS_Y,value);
    case AKJOY_BUTTON_RX: return akjoy_uinput_send(uinput,EV_ABS,ABS_RX,value);
    case AKJOY_BUTTON_RY: return akjoy_uinput_send(uinput,EV_ABS,ABS_RY,value);
    
    // Dpad arriving as axes.
    case AKJOY_BUTTON_DX: return akjoy_uinput_send(uinput,EV_ABS,ABS_HAT0X,value);
    case AKJOY_BUTTON_DY: return akjoy_uinput_send(uinput,EV_ABS,ABS_HAT0Y,value);
  }
  return 0;
}

/* Synchronize.
 */
 
int akjoy_uinput_sync(struct akjoy_uinput *uinput) {
  if (!uinput->dirty) return 0;
  if (akjoy_uinput_send(uinput,EV_SYN,SYN_REPORT,0)<0) return -1;
  uinput->dirty=0;
  return 0;
}

/* Button names.
 */

const char *akjoy_button_repr(int btnid) {
  switch (btnid) {
  #define _(tag) case AKJOY_BUTTON_##tag: return #tag;
    _(LEFT)
    _(RIGHT)
    _(UP)
    _(DOWN)
    _(SOUTH)
    _(WEST)
    _(EAST)
    _(NORTH)
    _(BLACK)
    _(WHITE)
    _(L1)
    _(R1)
    _(L2)
    _(R2)
    _(LP)
    _(RP)
    _(START)
    _(SELECT)
    _(HOME)
    _(LX)
    _(LY)
    _(RX)
    _(RY)
    _(DX)
    _(DY)
  #undef _
  }
  return 0;
}
