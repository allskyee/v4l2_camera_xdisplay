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
#include <opencv2/tracking/tracker.hpp>

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

void convert_yuv420_bgr888(const unsigned char* yuv, unsigned char* rgb, int width, int height)
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
		}
	}
}

#define WIDTH   640
#define HEIGHT  480

volatile int render_thread_fps = 0;

#define OBJS_MAX_N	10
pthread_spinlock_t objs_lock;
struct _obj_traj {
	cv::Rect rect;
	int valid;
} obj_traj[OBJS_MAX_N];

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

		pthread_spin_lock(&objs_lock);
		for (i = 0; i < OBJS_MAX_N; i++) {
			if (!obj_traj[i].valid)
				continue;
			cv::rectangle(image, obj_traj[i].rect, 255);
		}
		pthread_spin_unlock(&objs_lock);
			
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

// face detection grabbed from
// http://docs.opencv.org/3.1.0/db/d28/tutorial_cascade_classifier.html#gsc.tab=0
// tracker requires https://github.com/opencv/opencv_contrib
// sample code in http://docs.opencv.org/3.1.0/d2/d0a/tutorial_introduction_to_tracker.html#gsc.tab=0
// sample code in http://docs.opencv.org/3.1.0/d5/d07/tutorial_multitracker.html#gsc.tab=0

void* tracker_thread(void* argv)
{
	struct pipe* p = (struct pipe*)argv;
	static char xml_path[128];
	static char image24[WIDTH * HEIGHT * 3];
	cv::CascadeClassifier face_cascade;
	cv::Mat frame8(HEIGHT, WIDTH, CV_8UC1);
	cv::Mat frame24(HEIGHT, WIDTH, CV_8UC3, image24);

	sprintf(xml_path, "%s/%s", OCV_PATH, 
		"share/OpenCV/haarcascades/haarcascade_frontalface_alt.xml");

	if (!face_cascade.load(xml_path)) {
		fprintf(stderr, "Error loading cascade!!\n");
		return (void*)-1;
	}

	while (!finish) {
		int buf_seq, i, objs_n;
		const void* buf;
		void* h;

		std::vector<cv::Rect> objs_rect;

		cv::MultiTracker trackers("TLD");
		std::vector<cv::Rect2d> objs_rect2d;

		/* 
		 * pull frame and detect objects
		 */
		if (!(h = pull_buf(p, 1, &buf, &buf_seq)))  {
			usleep(1000);
			continue;
		}

		frame8.data = (uchar*)buf;
		face_cascade.detectMultiScale( frame8, objs_rect, 1.1, 2, 
			cv::CASCADE_SCALE_IMAGE, cv::Size(30, 30) );

		if ((objs_n = objs_rect.size()) == 0) { 
			put_buf(p, h);
			continue;
		}
		objs_rect2d.resize(objs_n);

		printf("detected %d objects\n", objs_n);

		convert_yuv420_bgr888((const unsigned char*)buf, \
			(unsigned char*)image24, WIDTH, HEIGHT);
		put_buf(p, h);

		/* 
		 * initialize tracker with ROIs
		 */
		pthread_spin_lock(&objs_lock);
		for (i = 0; i < objs_n; i++) {
			obj_traj[i].valid = 1;
			obj_traj[i].rect = objs_rect[i];
		}
		pthread_spin_unlock(&objs_lock);

		for (i = 0; i < objs_n; i++) {
			objs_rect2d[i] = objs_rect[i];
			printf("%f %f %f %f\n", objs_rect2d[i].x, objs_rect2d[i].y, 
				objs_rect2d[i].width, objs_rect2d[i].height);
		}

		trackers.add(frame24, objs_rect2d);

		printf("start tracking\n");

		/* 
		 * pull frame and track objects
		 */
		while (!finish) {

			if (!(h = pull_buf(p, 1, &buf, &buf_seq)))  {
				usleep(1000);
				continue;
			}

			convert_yuv420_bgr888((const unsigned char*)buf, 
				(unsigned char*)image24, WIDTH, HEIGHT);
			put_buf(p, h);

			trackers.update(frame24);
			
			//printf("tracked %d\n", trackers.objects.size());

			pthread_spin_lock(&objs_lock);
			for (i = 0; i < trackers.objects.size(); i++) {
				obj_traj[i].valid = 1;
				obj_traj[i].rect = trackers.objects[i];
			}
			pthread_spin_unlock(&objs_lock);
		}

		/* 
		 * giving up ... destroy all tracker objects
		 */
		pthread_spin_lock(&objs_lock);
		for (i = 0; i < objs_n; i++) {
			obj_traj[i].valid = 0;
			// TODO : clear the line queues as well
		}
		pthread_spin_unlock(&objs_lock);
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
	if (pthread_spin_init(&objs_lock, PTHREAD_PROCESS_PRIVATE))  {
        fprintf(stderr, "unable to setup spinlock\n");
		exit(-1);
	}
	memset(&obj_traj, 0, sizeof(obj_traj));

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
	if (pthread_create(&threads[1], NULL, tracker_thread, &p)) {
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
