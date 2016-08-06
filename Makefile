CC=gcc
CFLAGS=-I. -DMOTION_V4L2 
LDFLAGS=-ljpeg -lc -lpthread -lX11
OCV=opencv-3.1.0/

v4l2_camera_xdisplay : main.c video2.c pipe.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ocv_fd_ot : main_fd_ot.c video2.c pipe.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pipe : pipe.c 
	$(CC) -o $@ $^ -DPIPE_TEST -lpthread

clean:
	rm -f *.o pipe v4l2_camera_xdisplay
