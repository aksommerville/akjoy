/* aj_decode.h
 * All the various binary reports we're able to decode.
 * (Standard USB, and specific to our devices).
 */
 
#ifndef AJ_DECODE_H
#define AJ_DECODE_H

#include <stdint.h>

struct aj_usb_summary;

/* Hardware we support.
 *********************************************************/

#define AJ_HWMODEL_NONE      0
#define AJ_HWMODEL_XBOX      1
#define AJ_HWMODEL_XBOX360   2
#define AJ_HWMODEL_N30       3
#define AJ_HWMODEL_SN30      4
#define AJ_HWMODEL_8BITDO    5 /* N30 or SN30 but we don't yet know which. */
 
struct aj_hardware_config {
  int hwmodel;
  int intfid;
  int altid;
  int epin;
};

/* After getting the summary of USB descriptors (see aj_usbdev_get_descriptors(),aj_usb_summarize()),
 * call this to analyze the summary businessly, and select a hardware model.
 * "NONE" is perfectly normal, it means drop the device.
 * You must zero (config) before calling.
 */
void aj_hardware_config_decide(struct aj_hardware_config *config,const struct aj_usb_summary *summary);

const char *aj_hwmodel_repr(int hwmodel);
int aj_report_length_for_hwmodel(int hwmodel);

/* Returns N30, SN30, or 8BITDO, nothing else.
 * Checks the buttons unique to each model.
 */
int aj_guess_8bitdo_model_from_report(const void *src,int srcc);

// True for XBOX,XBOX360,N30,SN30, ie the ones that represent a real definite model.
int aj_hwmodel_is_concrete(int hwmodel);

/* Declare the buttons and axes we are going to generate.
 * (lo,hi) ignored for buttons.
 * These functions deliberately all have the same signature, in case that's convenient.
 */
int aj_declare_caps_xbox(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata);
int aj_declare_caps_xbox360(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata);
int aj_declare_caps_n30(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata);
int aj_declare_caps_sn30(int (*cb)(int type,int code,int lo,int hi,void *userdata),void *userdata);

/* Given a fresh input report (src) and the prior state (pv), trigger (cb) for everything that changed.
 */
int aj_parse_report_xbox(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata);
int aj_parse_report_xbox360(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata);
int aj_parse_report_n30(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata);
int aj_parse_report_sn30(const void *src,const void *pv,int c,int (*cb)(int type,int code,int value,void *userdata),void *userdata);

/* Standard USB Descriptors.
 ********************************************************/

struct aj_usb_device_descriptor {
  uint16_t bcdUsb;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
};

struct aj_usb_configuration_descriptor {
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
};

struct aj_usb_interface_descriptor {
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
};

struct aj_usb_endpoint_descriptor {
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};
 
struct aj_usb_summary {
  struct aj_usb_device_descriptor device;
  struct aj_usb_configuration_descriptor configuration;
  int optionc;
  struct aj_usb_option {
    struct aj_usb_interface_descriptor interface;
    struct aj_usb_endpoint_descriptor epin;
  } optionv[];
};

/* The easy way.
 * Give us the full descriptors as reported by usbdevfs read() (device, then config+intf+endpoint+class).
 * We allocate a new summary report and return it.
 * Each interrupt-in endpoint generates one option. Please note:
 *  - An interface may appear in more than one option.
 *  - Output endpoints are not included. For now, we don't support LEDs or rumble motors or any of that.
 *  - Interfaces with no interrupt-in endpoints will not be reported at all.
 * Caller must free the returned summary.
 */
struct aj_usb_summary *aj_usb_summarize(const void *src,int srcc);
 
#define USB_DESC_DEVICE 1
#define USB_DESC_CONFIGURATION 2
#define USB_DESC_STRING 3
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT 5
#define USB_DESC_DEVICE_QUALIFIER 6
#define USB_DESC_OTHER_SPEED_CONFIGURATION 7
#define USB_DESC_INTERFACE_POWER1 8
#define USB_DESC_HID 0x21
#define USB_DESC_HID_REPORT 0x22
#define USB_DESC_HID_PHYSICAL 0x23

/* Return the length and type of a USB descriptor.
 * We will never return more than (srcc), and never less than zero.
 */
int aj_usb_descriptor_measure(int *type,const void *src,int srcc);

void aj_usb_device_descriptor_decode(struct aj_usb_device_descriptor *dst,const void *src,int srcc);
void aj_usb_configuration_descriptor_decode(struct aj_usb_configuration_descriptor *dst,const void *src,int srcc);
void aj_usb_interface_descriptor_decode(struct aj_usb_interface_descriptor *dst,const void *src,int srcc);
void aj_usb_endpoint_descriptor_decode(struct aj_usb_endpoint_descriptor *dst,const void *src,int srcc);

#endif
