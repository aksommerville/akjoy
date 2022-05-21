# akjoy

Userspace Linux driver for joysticks:
- Xbox
- Xbox 360
- 8bitdo N30 (NES)
- 8bitdo SN30 (SNES)

This conflicts with a standard kernel module 'xpad' -- you'll want to blacklist that if you prefer akjoy.
Drop xpad a la carte for testing: `sudo modprobe -r xpad`

## Install

No script. Do it manually because you'll want to verify things, and I'm not too confident.

1. Blacklist xpad: `/etc/modprobe.d/blacklist.conf` <= `blacklist xpad`
2. Install binary: `make && sudo cp out/akjoy /usr/local/bin/akjoy`
3. Create systemd service: `sudo cp etc/akjoy.service /etc/systemd/system/akjoy.service`
4. Enable: `sudo systemctl enable akjoy`
