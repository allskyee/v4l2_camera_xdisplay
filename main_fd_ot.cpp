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

#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>

unsigned short int debug_level;

volatile int finish = 0;

void sigint_handler(int signo)
{
	finish = 1;
}

#define clamp(v, m, M) \
	(((v) > (M)) ? (M) : ((v) < (m) ? (m) : (v)))

void convert_yuv420_bgra8888(const unsigned char* yuv, unsigned char* rgb, int width, int height)
{
	int w, h;
	unsigned char* p = rgb;
	const unsigned char* y = yuv;
	const unsigned char* u = y + (width * height);
	const unsigned char* v = u + ((width / 2) * (height / 2));

	for (h = 0; h < height; h++) {
		for (w = 0; w < width; w++) {
			int _y = y[h * width + w];
			int _u = u[(h / 2) * (width / 2) + (w / 2)];
			int _v = v[(h / 2) * (width / 2) + (w / 2)];

			int rTmp = _y + (1.370705 * (_v-128));
			int gTmp = _y - (0.698001 * (_v-128)) - (0.337633 * (_u-128));
			int bTmp = _y + (1.732446 * (_u-128));

			*p++ = clamp(bTmp, 0, 255); //blue
			*p++ = clamp(gTmp, 0, 255); //green
			*p++ = clamp(rTmp, 0, 255); //red
			*p++ = 255;
		}
	}
}

#define WIDTH   640
#define HEIGHT  480

volatile int render_thread_fps = 0;

#define FACES_MAX_N	10
pthread_spinlock_t faces_lock;
cv::Rect faces_all[FACES_MAX_N];
volatile int faces_n = 0;
volatile int faces_updated = 0;

void* render_thread(void* argv)
{
	struct pipe* p = (struct pipe*)argv;
    static char image32[WIDTH*HEIGHT*4];
    static char image16[WIDTH*HEIGHT*2];
    Display* display = XOpenDisplay(NULL);
    Visual* visual = DefaultVisual(display, 0);
    Window window = XCreateSimpleWindow(display, 
		RootWindow(display, 0), 0, 0, WIDTH, HEIGHT, 1, 0, 0);
    XImage* ximage = XCreateImage(display, visual, 24, ZPixmap, 0, 
		image32, WIDTH, HEIGHT, 32, 0);
    XMapWindow(display, window);
	struct timespec t_start;
	int seq;
	cv::Mat image(HEIGHT, WIDTH, 0, CV_MAT_CONT_FLAG);

	image.data = (uchar*)image16;

#if 0
	// doesn't compile with g++
    if(visual->class != TrueColor) {
		fprintf(stderr, "Cannot handle non true color visual ...\n");
		return (void*)-1;
    }
#endif

	clock_gettime(CLOCK_REALTIME, &t_start);
	for (seq = 0; !finish; ) {
		int buf_seq, i;
		const void* buf;
		void* h = pull_buf(p, 0, &buf, &buf_seq);

		if (!h) {
			usleep(1000);
			continue;
		}

		memcpy(image16, buf, WIDTH * HEIGHT * 2);
		put_buf(p, h);

		pthread_spin_lock(&faces_lock);
		for (i = 0; i < faces_n; i++) 
			cv::rectangle(image, faces_all[i], 255);
		pthread_spin_unlock(&faces_lock);
			
		convert_yuv420_bgra8888((const unsigned char*)image16, 
			(unsigned char*)image32, WIDTH, HEIGHT);

		XPutImage(display, window, DefaultGC(display, 0), 
							ximage, 0, 0, 0, 0, WIDTH, HEIGHT);

		if (seq == 30) {
			struct timespec t_now;
			int t_ms;

			clock_gettime(CLOCK_REALTIME, &t_now);
			t_ms = (t_now.tv_sec - t_start.tv_sec) * 1e3;
			t_ms += ((t_now.tv_nsec - t_start.tv_nsec) / 1e6);

			render_thread_fps = (seq * (int)1e3) / t_ms;

			t_start = t_now;

			seq = 0;
		}

		seq++;
	}

	return NULL;
}

// grabbed from
// http://docs.opencv.org/3.1.0/db/d28/tutorial_cascade_classifier.html#gsc.tab=0

void* face_detection_thread(void* argv)
{
	struct pipe* p = (struct pipe*)argv;
	char xml_path[128];
	cv::CascadeClassifier face_cascade;
	cv::Mat image(HEIGHT, WIDTH, (int)NULL, CV_MAT_CONT_FLAG);
	struct timespec t_start;

	sprintf(xml_path, "%s/%s", OCV_PATH, 
		"share/OpenCV/haarcascades/haarcascade_frontalface_alt.xml");

	if (!face_cascade.load(xml_path)) {
		fprintf(stderr, "Error loading cascade!!\n");
		return (void*)-1;
	}

	clock_gettime(CLOCK_REALTIME, &t_start);
	while (!finish) {
		struct timespec t_start, t_end;
		int buf_seq, t_ms;
		const void* buf;
		void* h;
		std::vector<cv::Rect> faces;

		clock_gettime(CLOCK_REALTIME, &t_start);

		flush_buf(p, 1);

		// obtain frame
		while(!(h = pull_buf(p, 1, &buf, &buf_seq))) 
			usleep(5000);

		// detect faces
		image.data = (uchar*)buf;
		face_cascade.detectMultiScale( image, faces, 1.1, 2, 
			cv::CASCADE_SCALE_IMAGE, cv::Size(30, 30) );

		// copy faces content onto render thread
		pthread_spin_lock(&faces_lock);
		faces_n = faces.size() < FACES_MAX_N ? faces.size() : FACES_MAX_N;
		faces_updated = 1;
		for (int i = 0; i < faces_n; i++) 
			faces_all[i] = faces[i];
		pthread_spin_unlock(&faces_lock);

		// put frame
		put_buf(p, h);

		printf("%d faces detected\n", faces.size());

		// calculate time incurred and sleep if appropriate
		clock_gettime(CLOCK_REALTIME, &t_end);
		t_ms = (t_end.tv_sec - t_start.tv_sec) * 1e3;
		t_ms += ((t_end.tv_nsec - t_start.tv_nsec) / 1e6);

		if (t_ms < 5000) //detect every second
			usleep(1000 * (5000 - t_ms));
	}

	return NULL;
}

int main(int argc, char* argv[])
{
	struct context ctxt = {0};
	struct pipe p;
	int seq, ret, seq_abs;
	struct timespec t_start;
	pthread_t threads[3] = {0};

	/* 
	 * setup locks
	 */
	if (pthread_spin_init(&faces_lock, PTHREAD_PROCESS_PRIVATE))  {
        fprintf(stderr, "unable to setup spinlock\n");
		exit(-1);
	}

	/* 
	 * setup signal
	 */
	if (signal(SIGINT,  sigint_handler) == SIG_ERR) {
        fprintf(stderr, "unable to register signal handler\n");
        exit(-1);
    }

	/* 
	 * setup pipe
	 */
	if (init_pipe(&p, 2, 2, WIDTH * HEIGHT * 2)) {
		fprintf(stderr, "unable to setup pipe\n");
		exit(-1);
	}

	/*
	 * setup threads
	 */
	if (pthread_create(&threads[0], NULL, render_thread, &p)) {
		fprintf(stderr, "unable to start render thread\n");
		exit(-1);
	}
	if (pthread_create(&threads[1], NULL, face_detection_thread, &p)) {
		fprintf(stderr, "unable to start face detection thread\n");
		exit(-1);
	}

	/* 
	 * setup webcam
	 */
	ctxt.conf.v4l2_palette = 8;
	ctxt.conf.brightness = 128;
	ctxt.conf.frame_limit = 30;
	ctxt.conf.input = 8;
	ctxt.conf.roundrobin_frames = 1;
	ctxt.conf.roundrobin_skip = 1;
	ctxt.conf.width = WIDTH;
	ctxt.conf.height = HEIGHT;
	ctxt.conf.video_device = "/dev/video0";

	//ctxt.imgs.type assigned in vid_v4l2_start()
	//also type is set statically to VIDEO_PALETTE_YUV420P in v4l2_start()

	vid_init();
	ret = vid_v4l2_start(&ctxt);
	if (ret == -1) {
		fprintf(stderr, "unable to open start v4l2\n");
		exit(-1);
	}

	/* 
	 * capture & display loop
	 */
	clock_gettime(CLOCK_REALTIME, &t_start);
	for (seq_abs = seq = 1; !finish; ) {
		void* buf;
		void* h = get_buf(&p, &buf);

		if (!h) { //no more empty so skipping!
			usleep(1000);
			continue;
		}

		vid_next(&ctxt, (unsigned char*)buf);
		push_buf(&p, h, seq_abs);

		if (seq == 30) {
			struct timespec t_now;
			int t_ms;

			clock_gettime(CLOCK_REALTIME, &t_now);
			t_ms = (t_now.tv_sec - t_start.tv_sec) * 1e3;
			t_ms += ((t_now.tv_nsec - t_start.tv_nsec) / 1e6);

			printf("%d \t %d\n", (seq * (int)1e3) / t_ms, 
				render_thread_fps);

			t_start = t_now;

			seq = 0;
		}

		seq++;
		seq_abs++;
	}

out_vid : 
	vid_close(&ctxt);

    return 0;
}
