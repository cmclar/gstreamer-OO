#ifndef GSTRECEIVER_H
#define GSTRECEIVER_H

#include "gstreamer-1.0/gst/gst.h"
#include <glib.h>
#include <iostream>
#include <thread>
#include <vector>
#include <map>

enum class source_types { USB, H264, H265 };

class gst_receiver
{
public:
	gst_receiver(int frame_width, int frame_height, int numchanels, 
				int port, source_types source_type,
				std::string net_interface="", std::string address = "");
	~gst_receiver();

	void run_gst_loop();
	void run();

	static GstFlowReturn new_sample(GstElement* sink, char* data);

	char* data;
	
private:
	void create_usb_source();
	void create_h264_source();
	void create_h265_source();

	GMainLoop* loop;

	GstElement *pipeline, *filter, *conv, *sink;

	std::vector<GstElement*> pipeline_elements;

	int src_frame_width;
	int src_frame_height;
	int src_frame_chanels;
	int src_port;

	std::string src_interface;
	std::string src_address;
		
};

#endif