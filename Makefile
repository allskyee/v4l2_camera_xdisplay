CC=gcc
CFLAGS=-I. -DMOTION_V4L2 
LDFLAGS=-ljpeg -lc -lpthread -lX11
SRCS=main.c video2.c pipe.c
OBJS=$(SRCS:.c=.o)

v4l2_camera_xdisplay : $(OBJS) global.h
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

pipe : pipe.c pipe.h
	$(CC) -o pipe pipe.c -DPIPE_TEST -lpthread

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

clean:
	rm -f *.o pipe v4l2_camera_xdisplay
