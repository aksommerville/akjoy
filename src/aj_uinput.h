/* aj_uinput.h
 * Wraps the output end, to uinput, for one device.
 */
 
#ifndef AJ_UINPUT_H
#define AJ_UINPUT_H

struct aj_uinput;

void aj_uinput_del(struct aj_uinput *uinput);

struct aj_uinput *aj_uinput_new();

/* Call once, between new and commit.
 * Corresponds to UI_DEV_SETUP.
 */
int aj_uinput_setup(
  struct aj_uinput *uinput,
  int vid,int pid,
  const char *name,int namec
);

/* Declare a key or axis.
 * We don't support other events types, but would be easy to add.
 */
int aj_uinput_add_key(struct aj_uinput *uinput,int code);
int aj_uinput_add_abs(struct aj_uinput *uinput,int code,int lo,int hi);

/* Call once when you're done declaring capabilities.
 * At this point, the device should appear on the input bus.
 * Corresponds to UI_DEV_CREATE.
 */
int aj_uinput_commit(struct aj_uinput *uinput);

/* Call sync to send SYN_REPORT if any events have gone out.
 */
int aj_uinput_event(struct aj_uinput *uinput,int type,int code,int value);
int aj_uinput_sync(struct aj_uinput *uinput);

#endif
