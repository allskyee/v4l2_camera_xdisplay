#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "global.h"

#include <unistd.h>

#include <X11/Xlib.h>

unsigned short int debug_level;

volatile int finish = 0;

void sigint_handler(int signo)
{
	finish = 1;
}

#define WIDTH   640
#define HEIGHT  480

#define clamp(v, m, M) \
	(((v) > (M)) ? (M) : ((v) < (m) ? (m) : (v)))

void convert_yuv420_bgra8888(unsigned char* yuv, unsigned char* rgb, int width, int height)
{
	int w, h;
	unsigned char* p = rgb;
	unsigned char* y = yuv;
	unsigned char* u = y + (width * height);
	unsigned char* v = u + ((width / 2) * (height / 2));

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

int main(int argc, char* argv[])
{
	static struct context ctxt = {0};
	static char in_buf[WIDTH*HEIGHT*2];
	static char out_buf[WIDTH*HEIGHT*2];
    static char image32[WIDTH*HEIGHT*4];
	int seq, ret;
	struct timespec t_start;
    Display *display;
    Visual *visual;
    Window window;
    XImage *ximage;

	/* 
	 * setup signal
	 */
	if (signal(SIGINT,  sigint_handler) == SIG_ERR) {
        fprintf(stderr, "unable to register signal handler\n");
        exit(0);
    }

	/* 
 	 * setup display
	 */
    display = XOpenDisplay(NULL);
    visual = DefaultVisual(display, 0);
    window = XCreateSimpleWindow(display, 
		RootWindow(display, 0), 0, 0, WIDTH, HEIGHT, 1, 0, 0);
    ximage = XCreateImage(display, visual, 24, ZPixmap, 0, 
		image32, WIDTH, HEIGHT, 32, 0);
    XMapWindow(display, window);

    if(visual->class!=TrueColor) {
		fprintf(stderr, "Cannot handle non true color visual ...\n");
		exit(0);
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
		exit(0);
	}

	/* 
	 * capture & display loop
	 */
	clock_gettime(CLOCK_REALTIME, &t_start);
	for (seq = 1; !finish; seq++) {
		vid_next(&ctxt, in_buf);

		if (seq == 30) {
			struct timespec t_now;
			int t_ms;

			clock_gettime(CLOCK_REALTIME, &t_now);
			t_ms = (t_now.tv_sec - t_start.tv_sec) * 1e3;
			t_ms += ((t_now.tv_nsec - t_start.tv_nsec) / 1e6);

			printf("%d fps\n", (seq * (int)1e3) / t_ms);

			t_start = t_now;

			seq = 0;
		}

		convert_yuv420_bgra8888(in_buf, image32, WIDTH, HEIGHT);

        XPutImage(display, window, DefaultGC(display, 0), 
			ximage, 0, 0, 0, 0, WIDTH, HEIGHT);

		usleep(1000);
	}

out_vid : 
	vid_close(&ctxt);

    return 0;
}
