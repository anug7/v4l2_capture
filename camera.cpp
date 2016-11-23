/*
 * camera.cpp
 *
 *  Created on: Nov 19, 2016
 *      Author: guna007
 */

#include <stdlib.h>
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>

#include "camera.hpp"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

using namespace std;

namespace v4l2_camera{

bool Camera::reinitialize_cam(){
	stop_capture();
	is_initialized = false;
	if(method == V4L2_MEMORY_MMAP)
		init_mmap();
	start_capture();
	return true;
}

bool Camera::init_device(char *file_name, int mode, uint32_t pixel_format){

	dev_name = (char*)malloc(sizeof(char) * strlen(file_name));
	strcpy(dev_name, file_name);
	int ret = open(dev_name, mode);
	if (ret < 0){
		cout << "Camera open failed: " << dev_name << "\n";
		return false;
	}
	id = ret;
	if(-1 == xioctl(id, VIDIOC_QUERYCAP, &cap)){
		cout << "Query cap error\n";
		return false;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		cout << "Cap Capture error\n";
		return false;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		cout << "Cap Streaming error\n";
		return false;
	}
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.height = h;
    fmt.fmt.pix.width = w;
    fmt.fmt.pix.pixelformat = pixel_format;
    if(-1 == xioctl(id, VIDIOC_S_FMT, &fmt)){
    	cout << "S FMT error\n";
    	return false;
    }
    w = fmt.fmt.pix.width;
    h = fmt.fmt.pix.height;
    img_size = fmt.fmt.pix.sizeimage;
    if(method == V4L2_MEMORY_MMAP)
    	return init_mmap();
    if(use_threading)
    	pthread_mutex_init(&lock, NULL);
	return true;
}

bool Camera::start_capture(){
	if(is_streaming){
		cout << "Streaming already started\n";
		return true;
	}
	enum v4l2_buf_type type;
	if(!is_initialized){
		if(method == V4L2_MEMORY_MMAP) init_mmap();
	}
	for (unsigned int i = 0; i < buffer_count; ++i) {
		CLEAR(v4l_buffer);
		v4l_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l_buffer.memory = V4L2_MEMORY_MMAP;
		v4l_buffer.index = i;

		if (-1 == xioctl(id, VIDIOC_QBUF, &v4l_buffer))
		{
			cout << "QBUF error\n";
			return false;
		}
	}
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl(id, VIDIOC_STREAMON, &type))
	{
		cout << "Stream ON error\n";
		return false;
	}
    is_streaming = true;
    pix_type = get_cv_type(fmt.fmt.pix.pixelformat);
	return true;
}

bool Camera::set_format(struct v4l2_format new_fmt){
    bool was_running = false;
	is_initialized = false;
	if(is_streaming) {
		was_running = true;
		stop_capture();
	}
	if(!free_buffers())
		return false;
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.height = new_fmt.fmt.pix.height;
	fmt.fmt.pix.width = new_fmt.fmt.pix.width;
	fmt.fmt.pix.pixelformat = new_fmt.fmt.pix.pixelformat;
	if(-1 == xioctl(id, VIDIOC_S_FMT, &fmt)){
		cout << "S FMT error\n";
		return false;
	}
	w = fmt.fmt.pix.width;
	h = fmt.fmt.pix.height;
	img_size = fmt.fmt.pix.sizeimage;
	if(method == V4L2_MEMORY_MMAP)
		return init_mmap();
	if(was_running)
		start_capture();
	return true;
}

bool Camera::get_format(struct v4l2_format *get_fmt){
	get_fmt->fmt.pix = fmt.fmt.pix;
	return true;
}

bool Camera::free_buffers(){
	CLEAR(v4l_buffer);
	for(unsigned int i = 0; i < buffer_count; i++){
		v4l_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l_buffer.memory = V4L2_MEMORY_MMAP;
		v4l_buffer.index = i;
		if (-1 == xioctl(id, VIDIOC_DQBUF, &v4l_buffer)) {
			switch (errno) {
			case EAGAIN:
				return -1;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				cout << "Error: VIDIOC_DQBUF\n";
				return false;
			}
		}
	}
	return true;
}

bool Camera::stop_capture(){
	if(is_streaming){
		enum v4l2_buf_type type;
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(id, VIDIOC_STREAMOFF, &type))
		{
			cout << "Streaming off error\n";
			return false;
		}
		is_streaming = false;
	}
	return true;
}

void Camera::capture_loop(void){
	Mat temp_img(Size(fmt.fmt.pix.width, fmt.fmt.pix.height), pix_type);
	if(use_threading){
		while(is_streaming){
			if (v4l2_select())
			{
				if(img_dqueue(temp_img)){
					pthread_mutex_lock(&lock);
					img_queue.push(temp_img);
					pthread_mutex_unlock(&lock);
				}
			}
			usleep(1000);
//			cout << "Cap loop for cam: " << id << "\n";
		}
	}
}

bool Camera::img_dqueue(Mat &temp_img){

	CLEAR(v4l_buffer);

	v4l_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l_buffer.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(id, VIDIOC_DQBUF, &v4l_buffer)) {
		switch (errno) {
		case EAGAIN:
			return -1;

		case EIO:
			/* Could ignore EIO, see spec. */

			/* fall through */

		default:
			cout << "Error: VIDIOC_DQBUF\n";
			return false;
		}
	}

	assert(v4l_buffer.index < buffer_count);

	img_buffers[v4l_buffer.index].len = v4l_buffer.bytesused;

	if (-1 == xioctl(id, VIDIOC_QBUF, &v4l_buffer))
	{
		cout << "Queue error\n";
		return false;
	}
	if(v4l_buffer.bytesused <= 0){
		return false;
	}
	memcpy((void*)temp_img.data, (void*)img_buffers[v4l_buffer.index].data, img_buffers[v4l_buffer.index].len);

	return true;
}

bool Camera::v4l2_select(){
	int retry = 3;
	unsigned int count = 5;
	while(count--){
		for (;;)
		{
			fd_set fds;
			struct timeval tv;
			FD_ZERO(&fds);
			FD_SET(id, &fds);
			tv.tv_sec = 2;
			tv.tv_usec = 0;
			int r = select(id + 1, &fds, NULL, NULL, &tv);
			if(-1 == r){
				cout << "Select Error\n";
				return false;
			}
			if(r == 0){
				cout << "Select timeout error\n";
				if (retry-- < 0){
					cout << "Retry limit reached exiting\n";
					cout << "Reinitializing Camera\n";
					reinitialize_cam();
					return false;
				}
				break;
			}
			if(r == 1){
				return true;
			}
		}
	}
}

bool Camera::read_frame(Mat &image_mat){
	bool flag = false;
	if(use_threading){
		pthread_mutex_lock(&lock);
		if(!img_queue.empty())
		{
			img_queue.front().copyTo(image_mat);
			flag = true;
		}
		if(img_queue.size() > buffer_count){
			img_queue.pop();
		}
		pthread_mutex_unlock(&lock);
	}else{
		if(v4l2_select()){
			image_mat = Mat(Size(fmt.fmt.pix.width, fmt.fmt.pix.height), pix_type);
			if(img_dqueue(image_mat)){
				flag = true;
			}
		}
	}
	return flag;
}

Camera::~Camera(){
	pthread_mutex_destroy(&lock);
	close(id);
	free(dev_name);
	free(img_buffers);
}

Camera::Camera(unsigned int width,unsigned int height):
		w(width), h(height){
	id = -1;
	dev_name = NULL;
	is_streaming = false;
	is_initialized = false;
	use_threading = false;
	method = V4L2_MEMORY_MMAP;
	img_size = 0;
	buffer_count = 4;
	img_buffers = (img_buffer*)malloc(sizeof(img_buffer) * buffer_count);
}

Camera::Camera(){

}

bool Camera::init_mmap(){

	CLEAR(req_buffers);
	req_buffers.count = buffer_count;
	req_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req_buffers.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(id, VIDIOC_REQBUFS, &req_buffers)) {
		if (EINVAL == errno) {
			cout << "Does not support mmap\n";
		} else {
			 cout << "Error in request buffer\n";
		}
		return false;
	}

	if (req_buffers.count < 2) {
		cout << "BUffer allocation error\n";
		return false;
	}

	for (uint32_t n_buffers = 0; n_buffers < req_buffers.count; ++ n_buffers) {
		CLEAR(v4l_buffer);

		v4l_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l_buffer.memory = V4L2_MEMORY_MMAP;
		v4l_buffer.index = n_buffers;

		if (-1 == xioctl(id, VIDIOC_QUERYBUF, &v4l_buffer))
		{
			cout << "Buffer allocation error\n";
			return false;
		}
		img_buffers[n_buffers].len = v4l_buffer.length;
		img_buffers[n_buffers].data = (char*)mmap(NULL /* start anywhere */, v4l_buffer.length,
		PROT_READ | PROT_WRITE /* required */,
		MAP_SHARED /* recommended */, id, v4l_buffer.m.offset);

		if (MAP_FAILED == img_buffers[n_buffers].data)
		{
			cout << "Buffer allocation error\n";
			return false;
		}
	}
	is_initialized = true;
	return true;
}

int xioctl(int fh, unsigned long int request, void *arg) {
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

int get_cv_type(uint32_t format){
	int type = 0;
	switch(format){

	case V4L2_PIX_FMT_RGB32:
		type = CV_8UC3;
		break;
	case V4L2_PIX_FMT_YUYV:
		type = CV_8UC2;
		break;
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_SBGGR8:
		type = CV_8UC1;
		break;
	default:
		type = CV_8UC1;
		break;
	}
	return type;
}

}
