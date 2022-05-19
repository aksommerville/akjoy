all:
.SILENT:
.SECONDARY:
PRECMD=echo "  $(@F)" ; mkdir -p $(@D) ;

CC:=gcc -c -MMD -O3 -Isrc -Werror -Wimplicit
LD:=gcc
LDPOST:=

CFILES:=$(shell find src -name '*.c')
OFILES:=$(patsubst src/%.c,mid/%.o,$(CFILES))
-include $(OFILES:.o=.d)
mid/%.o:src/%.c;$(PRECMD) $(CC) -o $@ $<

EXE:=out/akjoy
all:$(EXE)
$(EXE):$(OFILES);$(PRECMD) $(LD) -o $@ $^ $(LDPOST)

clean:;rm -rf mid out

test run:;echo "Please see Makefile for options." ; exit 1

# Run against specific devices.
# `lsusb` to find them. We can't automatically determine which device to listen to.
# (that's kind of the whole problem that akjoy is there to solve).
test-sn30   :$(EXE);sudo $(EXE) /dev/bus/usb/001/010 --dpad=flip
test-n30    :$(EXE);sudo $(EXE) /dev/bus/usb/001/013 --dpad=flip
test-xbox360:$(EXE);sudo $(EXE) /dev/bus/usb/001/007 --lstick=disable --rstick=disable --dpad=flip
test-xbox   :$(EXE);sudo $(EXE) /dev/bus/usb/001/012 --lstick=disable --rstick=disable --dpad=flip --thumb=binary --bw=binary --trigger=binary
