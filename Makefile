CFLAGS=-g -Wall -Werror `pkg-config --cflags gtk+-2.0` `pkg-config --cflags libpulse`
LDLIBS=`pkg-config --libs gtk+-2.0` `pkg-config --libs libpulse`

all: syn clutter-test

clutter-test: CFLAGS=-g -Wall -Werror `pkg-config --cflags clutter-1.0`
clutter-test: LDLIBS=-g -Wall `pkg-config --libs clutter-1.0`


