
LIBS=-lasound
CFLAGS=-g -Wall -pedantic $(LIBS)

.PHONY : clean all

BINS=lsmi-monterey lsmi-joystick lsmi-mouse lsmi-keyhack lsmi-gamepad-toggle-cc

all: $(BINS)

clean:
	rm -f $(BINS)

seq.o: seq.c seq.h

sig.o: sig.c

OBJS=seq.o sig.o

lsmi-monterey: lsmi-monterey.c $(OBJS)

lsmi-joystick: lsmi-joystick.c $(OBJS)

lsmi-mouse: lsmi-mouse.c $(OBJS)

lsmi-keyhack: lsmi-keyhack.c $(OBJS)

lsmi-gamepad-toggle-cc: lsmi-gamepad-toggle-cc.c $(OBJS)

install: $(BINS)
	install $(BINS) /usr/local/bin

