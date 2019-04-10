#ifndef GSTTRANSMITTER_H
#define GSTTRANSMITTER_H

#include "gstreamer-1.0/gst/gst.h"
#include "gstreamer-1.0/gst/video/video.h"
#include "gstreamer-1.0/gst/app/gstappsrc.h"
#include "opencv2/opencv.hpp"
#include <glib.h>
#include <iostream>
#include <thread>
#include <vector>
#include <map>

enum class dst_types { H264, H265, DISP };

class gst_transmitter
{
public:
	gst_transmitter(int frame_width, int frame_height, int frame_rate, int port,
		dst_types dst_type, cv::Mat& frame, const char* address = "");
	~gst_transmitter();

	void run_gst_loop();
	void run();

	void update_last_frame(cv::Mat& frame);

private:
	void create_h264_pipe();
	void create_h265_pipe();
	void create_display();

	GMainLoop* loop;

	GstElement *pipeline, *queue, *source, *identity, *conv, *filter, *sink;

	std::vector<GstElement*> pipeline_elements;

	std::string dst_address;

	int dst_port;
	int dst_frame_width;
	int dst_frame_height;
	int dst_framerate_hz;

	int num_bytes;
	GstFlowReturn ret;
	GstMapInfo map;
	gint num_samples;

	static guint sourceid;

};

#endif // !GSTTRANSMITTER_H
