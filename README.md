# akjoy

Userspace Linux driver for joysticks:
- Xbox
- Xbox 360
- 8bitdo N30 (NES)
- 8bitdo SN30 (SNES)

This conflicts with a standard kernel module 'xpad' -- you'll want to blacklist that if you prefer akjoy.
Drop xpad a la carte for testing: `sudo modprobe -r xpad`
