#include "aj_device.h"
#include "aj_usbdev.h"
#include "aj_decode.h"
#include "aj_uinput.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input-event-codes.h>

/* Object definition.
 */
 
struct aj_device {
  struct aj_usbdev *usbdev;
  struct aj_uinput *uinput;
  struct aj_hardware_config config;
  int busid,devid;
  int rptlen;
  void *pvrpt;
};

/* Delete.
 */

void aj_device_del(struct aj_device *device) {
  if (!device) return;
  
  aj_usbdev_del(device->usbdev);
  aj_uinput_del(device->uinput);
  if (device->pvrpt) free(device->pvrpt);
  
  free(device);
}

/* Init uinput. Call only when config.hwmodel becomes concrete.
 */
 
static int aj_device_cb_cap(int type,int code,int lo,int hi,void *userdata) {
  struct aj_device *device=userdata;
  switch (type) {
    case EV_KEY: if (aj_uinput_add_key(device->uinput,code)<0) return -1; break;
    case EV_ABS: if (aj_uinput_add_abs(device->uinput,code,lo,hi)<0) return -1; break;
    default: return -1;
  }
  return 0;
}
 
static int aj_device_init_uinput(struct aj_device *device) {
  if (device->uinput) return -1;
  if (!(device->uinput=aj_uinput_new())) return -1;
  
  switch (device->config.hwmodel) {
  
    //TODO We are making up vid, pid, and name. If we wanted to, we could grab them from the USB descriptors.
    // Better to override for the 8bitdo ones, since they masquerade as Xbox 360.
  
    case AJ_HWMODEL_XBOX: {
        if (aj_declare_caps_xbox(aj_device_cb_cap,device)<0) return -1;
        if (aj_uinput_setup(device->uinput,0x045e,0x0289,"Xbox",4)<0) return -1;
      } break;
      
    case AJ_HWMODEL_XBOX360: {
        if (aj_declare_caps_xbox360(aj_device_cb_cap,device)<0) return -1;
        if (aj_uinput_setup(device->uinput,0x045e,0x028e,"Xbox 360",8)<0) return -1;
      } break;
      
    case AJ_HWMODEL_N30: {
        if (aj_declare_caps_n30(aj_device_cb_cap,device)<0) return -1;
        if (aj_uinput_setup(device->uinput,0x2dc8,0xfac0,"8bitdo NES",10)<0) return -1;
      } break;
      
    case AJ_HWMODEL_SN30: {
        if (aj_declare_caps_sn30(aj_device_cb_cap,device)<0) return -1;
        if (aj_uinput_setup(device->uinput,0x2dc8,0x16bb,"8bitdo SNES",11)<0) return -1;
      } break;
      
    default: return -1;
  }
  
  if (aj_uinput_commit(device->uinput)<0) return -1;
  
  return 0;
}

/* New.
 */

struct aj_device *aj_device_new(
  struct aj_usbdev *usbdev,
  const struct aj_hardware_config *config,
  int busid,int devid
) {
  if (!usbdev||!config) return 0;
  if (!config->hwmodel) return 0;
  
  if (aj_usbdev_set_interface(usbdev,config->intfid,config->altid,config->epin)<0) return 0;
  
  struct aj_device *device=calloc(1,sizeof(struct aj_device));
  if (!device) return 0;
  
  device->config=*config;
  device->busid=busid;
  device->devid=devid;
  
  if (
    ((device->rptlen=aj_report_length_for_hwmodel(config->hwmodel))<0)||
    !(device->pvrpt=calloc(1,device->rptlen))
  ) {
    aj_device_del(device);
    return 0;
  }
  
  if (aj_usbdev_submit_input_request(usbdev,device->rptlen)<0) {
    aj_device_del(device);
    return 0;
  }
  
  if (aj_hwmodel_is_concrete(config->hwmodel)) {
    if (aj_device_init_uinput(device)<0) {
      aj_device_del(device);
      return 0;
    }
  }
  
  // usbdev is HANDOFF, so don't assign it until we're ready to succeed.
  device->usbdev=usbdev;
  return device;
}

/* Trivial accessors.
 */
 
int aj_device_get_busid(const struct aj_device *device) {
  if (!device) return 0;
  return device->busid;
}

int aj_device_get_devid(const struct aj_device *device) {
  if (!device) return 0;
  return device->devid;
}

struct aj_usbdev *aj_device_get_usbdev(const struct aj_device *device) {
  if (!device) return 0;
  return device->usbdev;
}

/* Event callback from report parser.
 */
 
static int aj_cb_event(int type,int code,int value,void *userdata) {
  struct aj_device *device=userdata;
  if (aj_uinput_event(device->uinput,type,code,value)<0) return -1;
  return 0;
}

/* Receive report.
 */
 
static int aj_device_receive_report(struct aj_device *device,const uint8_t *src,int srcc) {
  if (srcc!=device->rptlen) {
    fprintf(stderr,"ignoring %d-byte input report, expected %d\n",srcc,device->rptlen);
    return 0;
  }
  
  // The MS ones don't send redundant reports, but the 8bitdo ones do, oh my do they.
  if (!memcmp(src,device->pvrpt,srcc)) return 0;
  
  //fprintf(stderr,"INPUT REPORT FROM USB:");
  //int i=0; for (;i<srcc;i++) fprintf(stderr," %02x",src[i]);
  //fprintf(stderr,"\n");
  
  // If we are still "undetermined 8bitdo device", look for the unique buttons.
  // We could just create the uinput device anyway, call it "NES or SNES", declare all 9 buttons, yadda yadda.
  // But then three little problems:
  //  - They'd have the same device name, apps won't be able to distinguish either.
  //  - N30 will have a bunch of dead declared buttons. (not a big deal really, like every joystick does that).
  //  - N30 (A,B) are the same as SN30 (B,A). I want them to be (B,Y). Can't manage that without knowing which device we're talking to.
  // I'm OK with the hassle of pressing Home or L/R before the device becomes usable, at each boot.
  // If you're not, then change aj_decode.c:aj_hardware_config_decide() to make a concrete decision.
  if (device->config.hwmodel==AJ_HWMODEL_8BITDO) {
    device->config.hwmodel=aj_guess_8bitdo_model_from_report(src,srcc);
    if (device->config.hwmodel==AJ_HWMODEL_8BITDO) {
      memcpy(device->pvrpt,src,srcc);
      return 0;
    }
    if (aj_device_init_uinput(device)<0) return -1;
  }
  
  switch (device->config.hwmodel) {
    case AJ_HWMODEL_XBOX: if (aj_parse_report_xbox(src,device->pvrpt,srcc,aj_cb_event,device)<0) return -1; break;
    case AJ_HWMODEL_XBOX360: if (aj_parse_report_xbox360(src,device->pvrpt,srcc,aj_cb_event,device)<0) return -1; break;
    case AJ_HWMODEL_N30: if (aj_parse_report_n30(src,device->pvrpt,srcc,aj_cb_event,device)<0) return -1; break;
    case AJ_HWMODEL_SN30: if (aj_parse_report_sn30(src,device->pvrpt,srcc,aj_cb_event,device)<0) return -1; break;
  }
  if (aj_uinput_sync(device->uinput)<0) return -1;
  
  memcpy(device->pvrpt,src,srcc);
  return 0;
}

/* Poll for input.
 */
 
int aj_device_poll(struct aj_device *device) {
  if (!device) return -1;
  const void *rpt=0;
  int rptc=aj_usbdev_poll(&rpt,device->usbdev);
  if (rptc<0) return -1;
  if (!rptc) return 0;
  if (aj_device_receive_report(device,rpt,rptc)<0) return -1;
  if (aj_usbdev_submit_input_request(device->usbdev,device->rptlen)<0) return -1;
  return 1;
}
