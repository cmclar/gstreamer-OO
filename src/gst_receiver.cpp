#include "gst_receiver.h"



gst_receiver::gst_receiver(int frame_width, int frame_height, int numchanels, int port,
							source_types source_type, std::string net_interface,
							std::string address)
	:src_frame_width{frame_width}, src_frame_height{frame_height}, src_port{port},
	src_frame_chanels{numchanels}, src_interface{net_interface}, src_address{address}
{
	data = (char*)malloc(sizeof(char) * (src_frame_width * src_frame_height * src_frame_chanels));

	/* Initialisation */
	gst_init(NULL, NULL);

	loop = g_main_loop_new(NULL, FALSE);

	switch (source_type)
	{
	case source_types::USB: create_usb_source(); break;
	case source_types::H264: create_h264_source(); break;
	case source_types::H265: create_h265_source(); break;
	default: create_usb_source(); break;
	}

	/* Create standard gstreamer elements for appsink pipeline */
	pipeline = gst_pipeline_new("webcam-player");
	conv = gst_element_factory_make("videoconvert", "converter");
	filter = gst_element_factory_make("capsfilter", NULL);
	sink = gst_element_factory_make("appsink", "appsink");

	if (!pipeline || !conv || !sink) {
		g_printerr("One element could not be created. Exiting.\n");
	}

	GstCaps* outcaps = gst_caps_new_simple("video/x-raw",
		"width", G_TYPE_INT, src_frame_width,
		"height", G_TYPE_INT, src_frame_height,
		"format", G_TYPE_STRING, "BGR", NULL);

	g_object_set(filter, "caps", outcaps, NULL);
	gst_caps_unref(outcaps);

	g_object_set(sink, "emit-signals", TRUE, "caps", NULL, NULL);
	g_signal_connect(sink, "new-sample", G_CALLBACK(new_sample), data);

	/* we add all elements into the pipeline */
	for each (GstElement* element in pipeline_elements)
	{
		gst_bin_add(GST_BIN(pipeline), element);
	}
	gst_bin_add_many(GST_BIN(pipeline), conv, filter, sink, NULL);

	/* we link the elements together */
	for(int i=0; i < pipeline_elements.size() - 1; i++) 
	{
		gst_element_link(pipeline_elements[i], pipeline_elements[i + 1]);
	}
	gst_element_link(pipeline_elements.back(), conv);
	gst_element_link(conv, filter);
	gst_element_link(filter, sink);

	/* Set the pipeline to "playing" state*/
	g_print("Now playing\n");
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
}


gst_receiver::~gst_receiver()
{
	/* Out of the main loop, clean up nicely */
	g_print("Returned, stopping playback\n");
	gst_element_set_state(pipeline, GST_STATE_NULL);

	g_print("Deleting pipeline\n");
	gst_object_unref(GST_OBJECT(pipeline));
	g_main_loop_unref(loop);
}

void gst_receiver::run_gst_loop()
{
	g_print("Running...\n");
	g_main_loop_run(loop);
}

void gst_receiver::run()
{
	std::thread t1(&gst_receiver::run_gst_loop, this);
	t1.detach();
}

void gst_receiver::create_usb_source()
{
	std::cout << "USB" << std::endl;
	GstElement* source = gst_element_factory_make("ksvideosrc", "ks-source");
	pipeline_elements.push_back(source);
	GstElement* srcfilter = gst_element_factory_make("capsfilter", NULL);
	pipeline_elements.push_back(srcfilter);

	GstCaps* incaps = gst_caps_new_simple("video/x-raw",
		"width", G_TYPE_INT, src_frame_width,
		"height", G_TYPE_INT, src_frame_height,
		"format", G_TYPE_STRING, "YUY2", NULL);

	g_object_set(srcfilter, "caps", incaps, NULL);
	gst_caps_unref(incaps);
}

void gst_receiver::create_h264_source()
{
	std::cout << "H264" << std::endl;

	GstElement* source = gst_element_factory_make("udpsrc", "udp-source");
	pipeline_elements.push_back(source);
	GstElement* srcfilter = gst_element_factory_make("capsfilter", NULL);
	pipeline_elements.push_back(srcfilter);
	GstElement* depay = gst_element_factory_make("rtph264depay", "h264 depayloader");
	pipeline_elements.push_back(depay);
	GstElement* parser = gst_element_factory_make("h264parse", "h264 parser");
	pipeline_elements.push_back(parser);
	GstElement* queue = gst_element_factory_make("queue", NULL);
	pipeline_elements.push_back(queue);
	GstElement* decoder = gst_element_factory_make("avdec_h264", "h264 decoder");
	pipeline_elements.push_back(decoder);

	GstCaps* incaps = gst_caps_new_simple("application/x-rtp",
		"encoding-name", G_TYPE_STRING, "H264",
		"payload", G_TYPE_INT, 96,
		NULL);

	g_object_set(srcfilter, "caps", incaps, NULL);
	g_object_set(source, "port", src_port, NULL);
	gst_caps_unref(incaps);
}

void gst_receiver::create_h265_source()
{
	std::cout << "H265" << std::endl;

	GstElement* source = gst_element_factory_make("udpsrc", "udp-source");
	pipeline_elements.push_back(source);
	GstElement* srcfilter = gst_element_factory_make("capsfilter", NULL);
	pipeline_elements.push_back(srcfilter);
	GstElement* depay = gst_element_factory_make("rtph265depay", "h265 depayloader");
	pipeline_elements.push_back(depay);
	GstElement* parser = gst_element_factory_make("h265parse", "h265 parser");
	pipeline_elements.push_back(parser);
	GstElement* queue = gst_element_factory_make("queue", NULL);
	pipeline_elements.push_back(queue);
	GstElement* decoder = gst_element_factory_make("avdec_h265", "h265 decoder");
	pipeline_elements.push_back(decoder);

	GstCaps* incaps = gst_caps_new_simple("application/x-rtp",
		"encoding-name", G_TYPE_STRING, "H265",
		"payload", G_TYPE_INT, 96,
		NULL);

	g_object_set(srcfilter, "caps", incaps, NULL);
	g_object_set(source, "port", src_port, NULL);
	gst_caps_unref(incaps);
}

GstFlowReturn gst_receiver::new_sample(GstElement* sink, char* data)
{
	GstSample* sample;
	g_signal_emit_by_name(sink, "pull-sample", &sample);
	GstBuffer* buffer = gst_sample_get_buffer(sample);
	GstMapInfo info;
	gst_buffer_map(buffer, &info, GST_MAP_READ);
	if (sample) {
		memcpy(data, info.data, info.size);
		gst_sample_unref(sample);
		gst_buffer_unmap(buffer, &info);
		return GST_FLOW_OK;
	}

	return GST_FLOW_ERROR;
}

bool gst_receiver::frame_available()
{
	return (&data[0] != 0) ? true : false;
}
