#ifndef AKJOY_H
#define AKJOY_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Configuration.
 *****************************************************/
 
#define AKJOY_MODEL_AUTO    0 /* Guess from input. */
#define AKJOY_MODEL_XBOX    1 /* Original Xbox. */
#define AKJOY_MODEL_XBOX360 2 /* Xbox 360. */
#define AKJOY_MODEL_N30     3 /* 8bitdo N30. */
#define AKJOY_MODEL_SN30    4 /* 8bitdo SN30. */

#define AKJOY_XFORM_DEFAULT    0 /* Pass events as they appear in the input. */
#define AKJOY_XFORM_DISABLE    1 /* Ignore events. */
#define AKJOY_XFORM_FLIP       2 /* Reverse Y axis (Xbox uses +up -down, everybody else does -up +down) */
#define AKJOY_XFORM_BINARY     3 /* Expose analogue inputs as binary, we make up a threshold. */

struct akjoy_config {
  const char *exename;
  char *devpath; // usbdevfs, eg "/dev/bus/usb/001/001"
  int hw_model; // AKJOY_MODEL_*, tells us how to interpret input reports
  char *name; // Name we submit to uinput. We can make one up.
  uint16_t vid,pid; // IDs to submit to uinput. Repeated from device if zero.
  int daemonize;
  
  // AKJOY_XFORM_*, each accepts only a subset.
  int dpad; // DEFAULT,DISABLE,FLIP
  int thumb; // DEFAULT,DISABLE,BINARY
  int bw; // DEFAULT,DISABLE,BINARY
  int aux; // DEFAULT,DISABLE
  int trigger; // DEFAULT,DISABLE,BINARY
  int lstick; // DEFAULT,DISABLE,FLIP
  int rstick; // DEFAULT,DISABLE,FLIP
};

void akjoy_config_cleanup(struct akjoy_config *config);

// >0 to proceed. Otherwise, we log something.
int akjoy_config_argv(struct akjoy_config *config,int argc,char **argv);

int akjoy_model_eval(const char *src,int srcc);
int akjoy_xform_eval(const char *src,int srcc);
int akjoy_int_eval(int *v,const char *src,int srcc);

/* USB device.
 *******************************************************/
 
#define AKJOY_REPORT_LENGTH 20
 
struct akjoy_usb_selection {
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t cfgid;
  uint8_t intfid;
  uint8_t altid;
  uint8_t epin;
  uint8_t epout;
};
  
struct akjoy_usb {
  int fd;
  struct akjoy_config *config; // WEAK
  
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0; // this is useful only because it's different 8bitdo vs Microsoft
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  
  struct akjoy_usb_selection sel; // Interface and endpoints we've chosen.
  struct akjoy_usb_selection seltmp; // During handshake.
  
  uint8_t rpt[AKJOY_REPORT_LENGTH];
  uint8_t pvrpt[AKJOY_REPORT_LENGTH];
};

void akjoy_usb_del(struct akjoy_usb *usb);

/* Logs errors.
 * Opening a device performs the initial "handshake", ie we read device and config descriptors.
 */
struct akjoy_usb *akjoy_usb_new(struct akjoy_config *config);

/* Wait for the next input report.
 * Returns <0 for real errors, eg disconnected.
 * 0 if timed out.
 * >0 if (rpt) and (pvrpt) have been populated. They could be identical, we don't read them.
 */
int akjoy_usb_update(struct akjoy_usb *usb);

/* Uinput device.
 *********************************************************/
 
#define AKJOY_BUTTON_LEFT       1 /* Dpad for Xbox controller. We will combine into two axes. */
#define AKJOY_BUTTON_RIGHT      2
#define AKJOY_BUTTON_UP         3
#define AKJOY_BUTTON_DOWN       4
#define AKJOY_BUTTON_SOUTH      5 /* thumb buttons... */
#define AKJOY_BUTTON_WEST       6 /* For N30, A is South and B is West. */
#define AKJOY_BUTTON_EAST       7 /* Because that's how Run and Jump map between SMB and SMW, so there. */
#define AKJOY_BUTTON_NORTH      8
#define AKJOY_BUTTON_BLACK      9 /* extra thumb-like buttons on Xbox only */
#define AKJOY_BUTTON_WHITE     10
#define AKJOY_BUTTON_L1        11 /* Facemost or only left trigger. Can be binary or analogue. */
#define AKJOY_BUTTON_R1        12
#define AKJOY_BUTTON_L2        13
#define AKJOY_BUTTON_R2        14
#define AKJOY_BUTTON_LP        15 /* Left plunger. Both Xboxes have it, always binary. */
#define AKJOY_BUTTON_RP        16
#define AKJOY_BUTTON_START     17 /* Remarkably, it's called Start and in a similar position on all of them. */
#define AKJOY_BUTTON_SELECT    18 /* aka Back */
#define AKJOY_BUTTON_HOME      19 /* Labelled Home on N30, and the big heart button on Xbox360. */
#define AKJOY_BUTTON_LX        20
#define AKJOY_BUTTON_LY        21
#define AKJOY_BUTTON_RX        22
#define AKJOY_BUTTON_RY        23
#define AKJOY_BUTTON_DX        24 /* dpad as axes, 8bitdo only */
#define AKJOY_BUTTON_DY        25
 
struct akjoy_uinput {
  int fd;
  struct akjoy_config *config; // WEAK
  int hw_model; // same as from (config) but mutable
  int dirty;
  int dx,dy; // aggregated dpad for xbox devices
  int analogue_thumbs; // nonzero if NORTH,SOUTH,WEST,EAST
  int analogue_bw;
  int analogue_triggers;
};

void akjoy_uinput_del(struct akjoy_uinput *uinput);

struct akjoy_uinput *akjoy_uinput_new(struct akjoy_config *config,struct akjoy_usb *usb);

int akjoy_uinput_configure_xbox(struct akjoy_uinput *uinput,struct akjoy_usb *usb);
int akjoy_uinput_configure_xbox360(struct akjoy_uinput *uinput,struct akjoy_usb *usb);
int akjoy_uinput_configure_n30(struct akjoy_uinput *uinput,struct akjoy_usb *usb);
int akjoy_uinput_configure_sn30(struct akjoy_uinput *uinput,struct akjoy_usb *usb);

/* Call sync at the end of each report. We'll send SYN_REPORT if anything went out since the last sync.
 */
int akjoy_uinput_event(struct akjoy_uinput *uinput,int btnid,int value);
int akjoy_uinput_sync(struct akjoy_uinput *uinput);

const char *akjoy_button_repr(int btnid);

#endif
