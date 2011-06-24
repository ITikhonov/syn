CFLAGS=-g -Wall -Werror `pkg-config --cflags libglfw` `pkg-config --cflags libpulse`
LDLIBS=`pkg-config --libs libglfw` `pkg-config --libs libpulse`

all: syn clutter-test icons.rgba

clutter-test: CFLAGS=-g -Wall -Werror `pkg-config --cflags clutter-1.0`
clutter-test: LDLIBS=-g -Wall `pkg-config --libs clutter-1.0`

icons.rgba: icons.png
	convert icons.png icons.rgba

icons.png: icons.svg
	echo EXPORT TO PNG


