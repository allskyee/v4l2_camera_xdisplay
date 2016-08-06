CC=gcc
CXX=g++
CFLAGS=-I. -DMOTION_V4L2 
LDFLAGS=-ljpeg -lc -lpthread -lX11
OCV=opencv-3.1.0/
OCV_PC=$(OCV)/lib/pkgconfig/opencv.pc
OCV_CFLAGS=`pkg-config --cflags $(OCV_PC)`
OCV_LDFLAGS=`pkg-config --libs $(OCV_PC)`

v4l2_camera_xdisplay : main.c video2.c pipe.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

v4l2_ocv_fd_ot : main_fd_ot.c video2.c pipe.c
	$(CXX) $(CFLAGS) -DOCV_PATH=$(OCV) $(OCV_CFLAGS) -o $@ $^ $(LDFLAGS) $(OCV_LDFLAGS) 

pipe : pipe.c 
	$(CC) -o $@ $^ -DPIPE_TEST -lpthread

clean:
	rm -f *.o pipe v4l2_camera_xdisplay
