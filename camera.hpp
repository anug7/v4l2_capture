/*
 * camera.hpp
 *
 *  Created on: Nov 19, 2016
 *      Author: guna007
 */

#include <iostream>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <errno.h>
#include <queue>
#include <pthread.h>

#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

namespace v4l2_camera{

	typedef struct img_buffer{
		unsigned int len;
		char *data;
	}img_buffer;

    int xioctl(int fh, unsigned long int request, void *arg);
    int get_cv_type(uint32_t format);
	class Camera{

    queue<Mat> img_queue;
	public:
		int id;
		char *dev_name;
		unsigned int method;
		unsigned int buffer_count;
		bool use_threading;

		Camera(unsigned int, unsigned int);
		Camera();
		~Camera();

		bool init_device(char *file_name, int mode, uint32_t pixel_format);
		bool start_capture();
		bool stop_capture();
		bool set_format(struct v4l2_format);
		bool get_format(struct v4l2_format*);
		bool read_frame(Mat& image);
		void capture_loop(void);
	private:
		struct v4l2_requestbuffers req_buffers;
		struct v4l2_buffer v4l_buffer;
		struct v4l2_capability cap;
		struct v4l2_captureparm cap_parm;
		struct v4l2_format fmt;
		bool is_streaming, is_initialized;
		unsigned int w, h, img_size;
		struct img_buffer *img_buffers;
		pthread_mutex_t lock;
		unsigned int pix_type;

		bool reinitialize_cam();
		bool init_mmap();
		bool v4l2_select();
		bool img_dqueue(Mat&);
		bool free_buffers();
	};
}
