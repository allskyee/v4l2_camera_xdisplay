CC=gcc
CXX=g++
CFLAGS=-I. -DMOTION_V4L2 
LDFLAGS=-ljpeg -lc -lpthread -lX11
OCV_PATH=opencv-3.1.0
OCV_PC=$(OCV_PATH)/lib/pkgconfig/opencv.pc
OCV_CFLAGS=`pkg-config --cflags $(OCV_PC)`
OCV_LDFLAGS=`pkg-config --libs $(OCV_PC)`

v4l2_camera_xdisplay : main.c video2.c pipe.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

v4l2_ocv_fd_ot : main_fd_ot.cpp video2.c pipe.c
	$(CXX) $(CFLAGS) -g -DOCV_PATH=\"$(OCV_PATH)\" $(OCV_CFLAGS) -o $@ $^ $(LDFLAGS) $(OCV_LDFLAGS) 

pipe : pipe.c 
	$(CC) -o $@ $^ -DPIPE_TEST -lpthread

clean:
	rm -f *.o pipe v4l2_camera_xdisplay
