CC=gcc
CFLAGS=-I. -DMOTION_V4L2 
LDFLAGS=-ljpeg -lc -lpthread -lX11
SRCS=main.c video2.c
OBJS=$(SRCS:.c=.o)

v4l2_camera_xdisplay : $(OBJS) global.h
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

clean:
	rm -f *.o mjpeg_streamer
