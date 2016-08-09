#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "global.h"
#include "pipe.h"

#include <unistd.h>
#include <X11/Xlib.h>

#include <opencv2/opencv.hpp>

#include <iostream>

using namespace std;
using namespace cv;

// captured from
// http://docs.opencv.org/3.1.0/d8/dfe/classcv_1_1VideoCapture.html#gsc.tab=0

int main(int argc, char* argv[])
{
	VideoCapture cap("2.mp4");

	if (!cap.isOpened()) {
		cerr << "unable to open" << endl;
		return -1;
	}

	namedWindow("MyVideo", CV_WINDOW_AUTOSIZE);

	while (true) {
		Mat frame, conv;

		if (!cap.read(frame))
			break;

		cvtColor(frame, conv, COLOR_RGB2GRAY);

		printf("type %d, rows %d, cols %d\n", 
			frame.type(), frame.rows, frame.cols);
		
		imshow("MyVideo", conv);
		if(waitKey(0) == 27) break;
	}

    return 0;
}
