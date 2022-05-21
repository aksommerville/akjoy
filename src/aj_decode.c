#include "aj_decode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <linux/input-event-codes.h>

/* Report-parsing bits.
 */
 
#define BITBTN(n,p,mask,code) { \
  if ((n)&mask) { \
    if (!((p)&mask)) { \
      int err=cb(EV_KEY,code,1,userdata); \
      if (err) return err; \
    } \
  } else if ((p)&mask) { \
    int err=cb(EV_KEY,code,0,userdata); \
    if (err) return err; \
  } \
}

// Analogue buttons we expose as 2-state. Using a threshold of 1.
#define BYTEBTN(n,p,code) { \
  if (n) { \
    if (!p) { \
      int err=cb(EV_KEY,code,1,userdata); \
      if (err) return err; \
    } \
  } else if (p) { \
    int err=cb(EV_KEY,code,0,userdata); \
    if (err) return err; \
  } \
}

#define AXISU8(n,p,code) if (n!=p) { \
  int err=cb(EV_ABS,code,n,userdata); \
  if (err) return err; \
}

// S8 axis we expose as -1,0,1 (input ranges are more likely -128,0,127)
#define AXIS8(n,p,code) if (n!=p) { \
  int err=cb(EV_ABS,code,(n&0x80)?-1:(n)?1:0,userdata); \
  if (err) return err; \
}

// S8 axis as (1,0,-1) -- The 8bitdo dpad is +up -down, I don't like that.
#define AXIS8R(n,p,code) if (n!=p) { \
  int err=cb(EV_ABS,code,(n&0x80)?1:(n)?-1:0,userdata); \
  if (err) return err; \
}

// s16le axis verbatim
#define AXIS16(n,p,code) { \
  int16_t _n=(n),_p=(p); \
  if (_n!=_p) { \
    int err=cb(EV_ABS,code,_n,userdata); \
    if (err) return err; \
  } \
}

// s16le axis inverted (Xbox Y axes are +up -down, I don't like that).
#define AXIS16R(n,p,code) { \
  int16_t _n=(n),_p=(p); \
  if (_n!=_p) { \
    if (_n==-32768) _n=32767; \
    else _n=-_n; \
    int err=cb(EV_ABS,code,_n,userdata); \
    if (err) return err; \
  } \
}

// Xbox dpad is 4 buttons, which is super cool, but games play better with axes.
#define XBOXDPAD(n,p) { \
  int err; \
  int _n=(n&0x03); \
  int _p=(p&0x03); \
  if (_n!=_p) switch (_n) { \
    case 0x01: if (err=cb(EV_ABS,ABS_HAT0Y,-1,userdata)) return err; break; \
    case 0x02: if (err=cb(EV_ABS,ABS_HAT0Y,1,userdata)) return err; break; \
    default: if (err=cb(EV_ABS,ABS_HAT0Y,0,userdata)) return err; break; \
  } \
  _n=(n&0x0c); \
  _p=(p&0x0c); \
  if (_n!=_p) switch (_n) { \
    case 0x04: if (err=cb(EV_ABS,ABS_HAT0X,-1,userdata)) return err; break; \
    case 0x08: if (err=cb(EV_ABS,ABS_HAT0X,1,userdata)) return err; break; \
    default: if (err=cb(EV_ABS,ABS_HAT0X,0,userdata)) return err; break; \
  } \
}

/* Original Xbox.
 */
 
int aj_declare_caps_xbox(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata) {
  int err;
  if (err=cb(EV_ABS,ABS_HAT0X,-1,1,userdata)) return err; // dpad, we compose it from buttons
  if (err=cb(EV_ABS,ABS_HAT0Y,-1,1,userdata)) return err; // ''
  if (err=cb(EV_ABS,ABS_X,-32768,32767,userdata)) return err; // lx
  if (err=cb(EV_ABS,ABS_Y,-32768,32767,userdata)) return err; // ly
  if (err=cb(EV_ABS,ABS_RX,-32768,32767,userdata)) return err; // rx
  if (err=cb(EV_ABS,ABS_RY,-32768,32767,userdata)) return err; // ry
  if (err=cb(EV_ABS,ABS_Z,0,255,userdata)) return err; // L
  if (err=cb(EV_ABS,ABS_RZ,0,255,userdata)) return err; // R
  if (err=cb(EV_KEY,BTN_SOUTH,0,0,userdata)) return err; // Most of the face buttons are 0..255 but we will report as 2-state
  if (err=cb(EV_KEY,BTN_WEST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_EAST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_NORTH,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_C,0,0,userdata)) return err; // Black ("C" as in "chyorniy" if it helps?)
  if (err=cb(EV_KEY,BTN_Z,0,0,userdata)) return err; // White ("Z" as in straight ones?)
  if (err=cb(EV_KEY,BTN_THUMBL,0,0,userdata)) return err; // These four really are 2-state...
  if (err=cb(EV_KEY,BTN_THUMBR,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_START,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_SELECT,0,0,userdata)) return err;
  return 0;
}

int aj_parse_report_xbox(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata) {
  if (c<20) return 0;
  const uint8_t *S=src,*P=pv;
  if (S[2]!=P[2]) {
    XBOXDPAD(S[2],P[2])
    BITBTN(S[2],P[2],0x10,BTN_START)
    BITBTN(S[2],P[2],0x20,BTN_SELECT)
    BITBTN(S[2],P[2],0x40,BTN_THUMBL)
    BITBTN(S[2],P[2],0x80,BTN_THUMBR)
  }
  BYTEBTN(S[4],P[4],BTN_SOUTH) // A
  BYTEBTN(S[5],P[5],BTN_EAST) // B
  BYTEBTN(S[6],P[6],BTN_WEST) // X
  BYTEBTN(S[7],P[7],BTN_NORTH) // Y
  BYTEBTN(S[8],P[8],BTN_C) // Black
  BYTEBTN(S[9],P[9],BTN_Z) // White
  AXISU8(S[10],P[10],ABS_Z) // L
  AXISU8(S[11],P[11],ABS_RZ) // R
  AXIS16 (S[12]|(S[13]<<8),P[12]|(P[13]<<8),ABS_X)
  AXIS16R(S[14]|(S[15]<<8),P[14]|(P[15]<<8),ABS_Y)
  AXIS16 (S[16]|(S[17]<<8),P[16]|(P[17]<<8),ABS_RX)
  AXIS16R(S[18]|(S[19]<<8),P[18]|(P[19]<<8),ABS_RY)
  return 0;
}

/* Xbox 360.
 */

int aj_declare_caps_xbox360(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata) {
  int err;
  if (err=cb(EV_ABS,ABS_HAT0X,-1,1,userdata)) return err; // dpad, we compose it from buttons
  if (err=cb(EV_ABS,ABS_HAT0Y,-1,1,userdata)) return err; // ''
  if (err=cb(EV_ABS,ABS_X,-32768,32767,userdata)) return err; // lx
  if (err=cb(EV_ABS,ABS_Y,-32768,32767,userdata)) return err; // ly
  if (err=cb(EV_ABS,ABS_RX,-32768,32767,userdata)) return err; // rx
  if (err=cb(EV_ABS,ABS_RY,-32768,32767,userdata)) return err; // ry
  if (err=cb(EV_ABS,ABS_Z,0,255,userdata)) return err; // L2
  if (err=cb(EV_ABS,ABS_RZ,0,255,userdata)) return err; // R2
  if (err=cb(EV_KEY,BTN_SOUTH,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_WEST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_EAST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_NORTH,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_TL,0,0,userdata)) return err; // L1
  if (err=cb(EV_KEY,BTN_TR,0,0,userdata)) return err; // R1
  if (err=cb(EV_KEY,BTN_START,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_SELECT,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_MODE,0,0,userdata)) return err; // Heart
  if (err=cb(EV_KEY,BTN_THUMBL,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_THUMBR,0,0,userdata)) return err;
  return 0;
}

int aj_parse_report_xbox360(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata) {
  if (c<20) return 0;
  const uint8_t *S=src,*P=pv;
  if (S[2]!=P[2]) {
    XBOXDPAD(S[2],P[2])
    BITBTN(S[2],P[2],0x10,BTN_START)
    BITBTN(S[2],P[2],0x20,BTN_SELECT)
    BITBTN(S[2],P[2],0x40,BTN_THUMBL)
    BITBTN(S[2],P[2],0x80,BTN_THUMBR)
  }
  if (S[3]!=P[3]) {
    BITBTN(S[3],P[3],0x01,BTN_TL)
    BITBTN(S[3],P[3],0x02,BTN_TR)
    BITBTN(S[3],P[3],0x04,BTN_MODE) // Heart
    BITBTN(S[3],P[3],0x10,BTN_SOUTH) // A
    BITBTN(S[3],P[3],0x20,BTN_EAST) // B
    BITBTN(S[3],P[3],0x40,BTN_WEST) // X
    BITBTN(S[3],P[3],0x80,BTN_NORTH) // Y
  }
  AXISU8(S[4],P[4],ABS_Z) // L2
  AXISU8(S[5],P[5],ABS_RZ) // R2
  AXIS16 (S[ 6]|(S[ 7]<<8),P[ 6]|(P[ 7]<<8),ABS_X)
  AXIS16R(S[ 8]|(S[ 9]<<8),P[ 8]|(P[ 9]<<8),ABS_Y)
  AXIS16 (S[10]|(S[11]<<8),P[10]|(P[11]<<8),ABS_RX)
  AXIS16R(S[12]|(S[13]<<8),P[12]|(P[13]<<8),ABS_RY)
  return 0;
}

/* N30.
 */

int aj_declare_caps_n30(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata) {
  int err;
  if (err=cb(EV_ABS,ABS_HAT0X,-1,1,userdata)) return err; // -128..127 on the device, but only sends 3 values.
  if (err=cb(EV_ABS,ABS_HAT0Y,-1,1,userdata)) return err; // ''
  if (err=cb(EV_KEY,BTN_SOUTH,0,0,userdata)) return err; // A
  if (err=cb(EV_KEY,BTN_WEST,0,0,userdata)) return err; // B
  if (err=cb(EV_KEY,BTN_START,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_SELECT,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_MODE,0,0,userdata)) return err; // Home
  return 0;
}

int aj_parse_report_n30(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata) {
  if (c<10) return 0;
  const uint8_t *S=src,*P=pv;
  if (S[2]!=P[2]) {
    BITBTN(S[2],P[2],0x10,BTN_START)
    BITBTN(S[2],P[2],0x20,BTN_SELECT)
  }
  if (S[3]!=P[3]) {
    BITBTN(S[3],P[3],0x04,BTN_MODE) // Home
    BITBTN(S[3],P[3],0x10,BTN_SOUTH) // A
    BITBTN(S[3],P[3],0x20,BTN_WEST) // B
  }
  AXIS8(S[7],P[7],ABS_HAT0X)
  AXIS8R(S[9],P[9],ABS_HAT0Y)
  return 0;
}

/* SN30.
 */

int aj_declare_caps_sn30(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata) {
  int err;
  if (err=cb(EV_ABS,ABS_HAT0X,-1,1,userdata)) return err; // -128..127 on the device, but only sends 3 values.
  if (err=cb(EV_ABS,ABS_HAT0Y,-1,1,userdata)) return err; // ''
  if (err=cb(EV_KEY,BTN_SOUTH,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_WEST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_EAST,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_NORTH,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_TL,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_TR,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_START,0,0,userdata)) return err;
  if (err=cb(EV_KEY,BTN_SELECT,0,0,userdata)) return err;
  return 0;
}

int aj_parse_report_sn30(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata) {
  if (c<10) return 0;
  const uint8_t *S=src,*P=pv;
  if (S[2]!=P[2]) {
    BITBTN(S[2],P[2],0x10,BTN_START)
    BITBTN(S[2],P[2],0x20,BTN_SELECT)
  }
  if (S[3]!=P[3]) {
    BITBTN(S[3],P[3],0x01,BTN_TL)
    BITBTN(S[3],P[3],0x02,BTN_TR)
    BITBTN(S[3],P[3],0x10,BTN_SOUTH) // B
    BITBTN(S[3],P[3],0x20,BTN_EAST) // A
    BITBTN(S[3],P[3],0x40,BTN_WEST) // Y
    BITBTN(S[3],P[3],0x80,BTN_NORTH) // X
  }
  AXIS8(S[7],P[7],ABS_HAT0X)
  AXIS8R(S[9],P[9],ABS_HAT0Y)
  return 0;
}

#undef BITBTN
#undef BYTEBTN
#undef AXISU8
#undef AXIS8
#undef AXIS8R
#undef AXIS16
#undef AXIS16R
#undef XBOXDPAD

/* Which 8bitdo model is this?
 */
 
int aj_guess_8bitdo_model_from_report(const void *src,int srcc) {
  if (srcc==20) {
    const uint8_t *SRC=src;
    // The N30 and SN30 reports are identical except 5 buttons:
    if (SRC[3]&0xc3) return AJ_HWMODEL_SN30; // L,R,X,Y
    if (SRC[3]&0x04) return AJ_HWMODEL_N30; // Home
  }
  return AJ_HWMODEL_8BITDO;
}

/* Is it a concrete model?
 */
 
int aj_hwmodel_is_concrete(int hwmodel) {
  switch (hwmodel) {
    case AJ_HWMODEL_XBOX:
    case AJ_HWMODEL_XBOX360:
    case AJ_HWMODEL_N30:
    case AJ_HWMODEL_SN30:
      return 1;
  }
  return 0;
}

/* Hardware config from summary.
 */
 
void aj_hardware_config_decide(struct aj_hardware_config *config,const struct aj_usb_summary *summary) {
  if (!config||!summary) return;
  
  // No usable endpoints? That's fine, drop it.
  if (summary->optionc<1) return;
  
  // Original Xbox must have zero for the device class, and a well-defined interface class.
  // Take the first matching option; mine only reports one.
  if (
    !summary->device.bDeviceClass&&
    !summary->device.bDeviceSubClass&&
    !summary->device.bDeviceProtocol
  ) {
    const struct aj_usb_option *option=summary->optionv;
    int i=summary->optionc;
    for (;i-->0;option++) {
      if (option->interface.bInterfaceClass!=0x58) continue;
      if (option->interface.bInterfaceSubClass!=0x42) continue;
      if (option->interface.bInterfaceProtocol!=0x00) continue;
      if (option->epin.wMaxPacketSize<20) continue;
      
      config->hwmodel=AJ_HWMODEL_XBOX;
      config->intfid=option->interface.bInterfaceNumber;
      config->altid=option->interface.bAlternateSetting;
      config->epin=option->epin.bEndpointAddress;
      return;
    }
  }
  
  // Xbox 360 has straight 255 for device class, and a well-defined interface class.
  // Unfortunately, both 8bitdos report exactly the same thing, even the same vendor and product IDs.
  // If we locate an "Xbox 360" interface, check bMaxPacketSize0: It's 8 for the 360, and 64 for the 8bitdos.
  // ^ That strategy is probably not valid for all devices, just happens to work for the ones I've got.
  // I'm going to further restrict the 8bitdos to just this vendor/product, but I think that's not necessary for all Xbox 360.
  if (
    (summary->device.bDeviceClass==0xff)&&
    (summary->device.bDeviceSubClass==0xff)&&
    (summary->device.bDeviceProtocol==0xff)
  ) {
    const struct aj_usb_option *option=summary->optionv;
    int i=summary->optionc;
    for (;i-->0;option++) {
      if (option->interface.bInterfaceClass!=0xff) continue;
      if (option->interface.bInterfaceSubClass!=0x5d) continue;
      // protocol is 1, 2, or 3: I don't know which.
      if (option->interface.bInterfaceProtocol!=0x01) continue;//TODO
      if (option->epin.wMaxPacketSize<20) continue;
      
      if (
        (summary->device.idVendor==0x045e)&&
        (summary->device.idProduct==0x28e)&&
        (summary->device.bMaxPacketSize0==64)
      ) {
        config->hwmodel=AJ_HWMODEL_8BITDO;
      } else {
        config->hwmodel=AJ_HWMODEL_XBOX360;
      }
      config->intfid=option->interface.bInterfaceNumber;
      config->altid=option->interface.bAlternateSetting;
      config->epin=option->epin.bEndpointAddress;
      return;
    }
  }
  
  // OK, not a device we know.
}

/* Hardware model names.
 */
 
const char *aj_hwmodel_repr(int hwmodel) {
  switch (hwmodel) {
    case AJ_HWMODEL_NONE: return "none";
    case AJ_HWMODEL_XBOX: return "Xbox";
    case AJ_HWMODEL_XBOX360: return "Xbox 360";
    case AJ_HWMODEL_N30: return "N30";
    case AJ_HWMODEL_SN30: return "SN30";
    case AJ_HWMODEL_8BITDO: return "8bitdo (id pending)";
  }
  return "unknown";
}

/* Report lengths. It's always 20, but we can change that pretty easy.
 */
 
int aj_report_length_for_hwmodel(int hwmodel) {
  switch (hwmodel) {
    case AJ_HWMODEL_XBOX:
    case AJ_HWMODEL_XBOX360:
    case AJ_HWMODEL_N30:
    case AJ_HWMODEL_SN30:
    case AJ_HWMODEL_8BITDO:
      return 20;
  }
  return 0;
}

/* Summarize descriptor set.
 */
 
struct aj_usb_summary *aj_usb_summarize(const void *src,int srcc) {
  if (!src||(srcc<1)) return 0;
  int optiona=8,srcp=0;
  struct aj_usb_summary *summary=calloc(1,sizeof(struct aj_usb_summary)+sizeof(struct aj_usb_option)*optiona);
  if (!summary) return 0;
  struct aj_usb_interface_descriptor intf={0};
  while (srcp<srcc) {
    int len,type;
    if ((len=aj_usb_descriptor_measure(&type,(char*)src+srcp,srcc-srcp))<2) break;
    switch (type) {
      
      case USB_DESC_DEVICE: aj_usb_device_descriptor_decode(&summary->device,(char*)src+srcp,len); break;
      case USB_DESC_CONFIGURATION: aj_usb_configuration_descriptor_decode(&summary->configuration,(char*)src+srcp,len); break;
      case USB_DESC_INTERFACE: aj_usb_interface_descriptor_decode(&intf,(char*)src+srcp,len); break;
      
      case USB_DESC_ENDPOINT: {
          struct aj_usb_endpoint_descriptor ep={0};
          aj_usb_endpoint_descriptor_decode(&ep,(char*)src+srcp,len);
          if ((ep.bmAttributes&3)!=3) break; // interrupt only
          if (!(ep.bEndpointAddress&0x80)) break; // input only
          if (summary->optionc>=optiona) {
            optiona+=8;
            if (optiona>512) break; // sanity limit, this is just too many options
            void *nv=realloc(summary,sizeof(struct aj_usb_summary)+sizeof(struct aj_usb_option)*optiona);
            if (!nv) {
              free(summary);
              return 0;
            }
            summary=nv;
          }
          struct aj_usb_option *option=summary->optionv+summary->optionc++;
          memcpy(&option->interface,&intf,sizeof(struct aj_usb_interface_descriptor));
          memcpy(&option->epin,&ep,sizeof(struct aj_usb_endpoint_descriptor));
        } break;
        
    }
    srcp+=len;
  }
  return summary;
}

/* Measure USB descriptor.
 */
 
int aj_usb_descriptor_measure(int *type,const void *src,int srcc) {
  if (!src) return 0;
  if (srcc<2) return 0;
  const uint8_t *SRC=src;
  int bLength=SRC[0];
  if (bLength>srcc) bLength=srcc;
  if (type) *type=SRC[1];
  return bLength;
}

/* Decode one descriptor.
 */
 
void aj_usb_device_descriptor_decode(struct aj_usb_device_descriptor *dst,const void *src,int srcc) {
  if (!src||(srcc<18)) return;
  const uint8_t *SRC=src;
  if (SRC[0]<18) return;
  if (SRC[1]!=USB_DESC_DEVICE) return;
  dst->bcdUsb=SRC[2]|(SRC[3]<<8);
  dst->bDeviceClass=SRC[4];
  dst->bDeviceSubClass=SRC[5];
  dst->bDeviceProtocol=SRC[6];
  dst->bMaxPacketSize0=SRC[7];
  dst->idVendor=SRC[8]|(SRC[9]<<8);
  dst->idProduct=SRC[10]|(SRC[11]<<8);
  dst->bcdDevice=SRC[12]|(SRC[13]<<8);
  dst->iManufacturer=SRC[14];
  dst->iProduct=SRC[15];
  dst->iSerialNumber=SRC[16];
  dst->bNumConfigurations=SRC[17];
}

void aj_usb_configuration_descriptor_decode(struct aj_usb_configuration_descriptor *dst,const void *src,int srcc) {
  if (!src||(srcc<9)) return;
  const uint8_t *SRC=src;
  if (SRC[0]<9) return;
  if (SRC[1]!=USB_DESC_CONFIGURATION) return;
  dst->wTotalLength=SRC[2]|(SRC[3]<<8);
  dst->bNumInterfaces=SRC[4];
  dst->bConfigurationValue=SRC[5];
  dst->iConfiguration=SRC[6];
  dst->bmAttributes=SRC[7];
  dst->bMaxPower=SRC[8];
}

void aj_usb_interface_descriptor_decode(struct aj_usb_interface_descriptor *dst,const void *src,int srcc) {
  if (!src||(srcc<9)) return;
  const uint8_t *SRC=src;
  if (SRC[0]<9) return;
  if (SRC[1]!=USB_DESC_INTERFACE) return;
  dst->bInterfaceNumber=SRC[2];
  dst->bAlternateSetting=SRC[3];
  dst->bNumEndpoints=SRC[4];
  dst->bInterfaceClass=SRC[5];
  dst->bInterfaceSubClass=SRC[6];
  dst->bInterfaceProtocol=SRC[7];
  dst->iInterface=SRC[8];
}

void aj_usb_endpoint_descriptor_decode(struct aj_usb_endpoint_descriptor *dst,const void *src,int srcc) {
  if (!src||(srcc<2)) return;
  const uint8_t *SRC=src;
  if (SRC[0]<7) return;
  if (SRC[1]!=USB_DESC_ENDPOINT) return;
  dst->bEndpointAddress=SRC[2];
  dst->bmAttributes=SRC[3];
  dst->wMaxPacketSize=SRC[4]|(SRC[5]<<8);
  dst->bInterval=SRC[6];
}
