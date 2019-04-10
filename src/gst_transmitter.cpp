#include "gst_transmitter.h"

cv::Mat* out_frame;
guint sourceid;

void buffer_destroy(gpointer data) {
	cv::Mat* done = (cv::Mat*)data;
	delete done;
}

static gboolean feed_data(GstAppSrc *appsrc)
{
	guint num_bytes = out_frame->total() * out_frame->elemSize();
	GstBuffer* buffer;
	GstFlowReturn ret;
	GstMapInfo map;
	static GstClockTime timestamp = 0;
	gboolean ok = TRUE;

	buffer = gst_buffer_new_and_alloc(num_bytes);

	gst_buffer_fill(buffer, 0, out_frame->data, num_bytes);

	GST_BUFFER_PTS(buffer) = timestamp;
	GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 4);

	timestamp += GST_BUFFER_DURATION(buffer);

	ret = gst_app_src_push_buffer(appsrc, buffer);

	if (ret != GST_FLOW_OK) {
		/* some error, stop sending data */
		GST_DEBUG("some error");
		ok = FALSE;
	}

	return ok;
}

static void stop_data(GstAppSrc *appsrc)
{
	if (sourceid != 0)
	{
		g_source_remove(sourceid);
		sourceid = 0;
	}	
}

static void push_data(GstAppSrc *appsrc,
	guint       unused_size,
	cv::Mat*    user_data)
{
	if (sourceid == 0)
	{
		sourceid = g_idle_add((GSourceFunc)feed_data, appsrc);
	}		
}

static gboolean bus_message(GstBus * bus, GstMessage * message, GMainLoop* loop)
{
	GST_DEBUG("got message %s", gst_message_type_get_name(GST_MESSAGE_TYPE(message)));
	g_print(gst_message_type_get_name(GST_MESSAGE_TYPE(message)));
	g_print("\n");

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_ERROR:
		g_error("received error");
		g_main_loop_quit(loop);
		break;
	case GST_MESSAGE_EOS:
		g_main_loop_quit(loop);
		break;
	default:
		break;
	}
	return TRUE;
}

gst_transmitter::gst_transmitter(int frame_width, int frame_height, int frame_rate,
	int port, dst_types dst_type, cv::Mat& frame, const char* address)
	:dst_frame_width{ frame_width }, dst_frame_height{ frame_height },
	dst_address{ address }, dst_framerate_hz{ frame_rate }, dst_port{ port }
{
	out_frame = &frame;
	num_bytes = frame.total() * frame.elemSize();

	gst_init(NULL, NULL);

	loop = g_main_loop_new(NULL, FALSE);

	switch (dst_type)
	{
	case dst_types::H264: create_h264_pipe();
		filter = gst_element_factory_make("capsfilter", NULL);
		sink = gst_element_factory_make("udpsink", NULL);
		g_object_set(sink, "host", dst_address, "port", dst_port, "sync", FALSE, "async", FALSE, NULL);
		g_object_set(filter, "caps", gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", NULL), NULL);
		break;

	case dst_types::H265: create_h265_pipe();
		filter = gst_element_factory_make("capsfilter", NULL);
		sink = gst_element_factory_make("udpsink", NULL);
		g_object_set(sink, "host", dst_address, "port", dst_port, "sync", FALSE, "async", FALSE, NULL);
		g_object_set(filter, "caps", gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", NULL), NULL);
		break;

	case dst_types::DISP:
		sink = gst_element_factory_make("autovideosink", NULL);

		break;

	default: create_h264_pipe(); break;
	}

	pipeline = gst_pipeline_new("ethernet-streamer");
	source = gst_element_factory_make("appsrc", "app-source");
	queue = gst_element_factory_make("queue", NULL);
	conv = gst_element_factory_make("videoconvert", NULL);

	GstCaps* incaps = gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "BGR",
		"width", G_TYPE_INT, dst_frame_width,
		"height", G_TYPE_INT, dst_frame_height,
		"framerate", GST_TYPE_FRACTION, dst_framerate_hz, 1, NULL);

	g_object_set(source, "caps", incaps, NULL);

	if (dst_type != dst_types::DISP)
	{
		for (GstElement* element : pipeline_elements)
		{
			gst_bin_add(GST_BIN(pipeline), element);
		}
		gst_bin_add_many(GST_BIN(pipeline), source, queue, filter, conv, sink, NULL);

		gst_element_link(source, queue);
		gst_element_link(queue, conv);
		gst_element_link(conv, filter);
		gst_element_link(filter, pipeline_elements.front());
		for (int i = 0; i < pipeline_elements.size() - 1; i++)
		{
			gst_element_link(pipeline_elements[i], pipeline_elements[i + 1]);
		}
		gst_element_link(pipeline_elements.back(), sink);
	}
	else
	{
		gst_bin_add_many(GST_BIN(pipeline), source, queue, conv, sink, NULL);
		gst_element_link(source, queue);
		gst_element_link(queue, conv);
		gst_element_link(conv, sink);
	}

	GstBus* bus = gst_pipeline_get_bus((GstPipeline*)pipeline);

	gst_bus_add_watch(bus, (GstBusFunc)bus_message, loop);
	gst_object_unref(bus);

	g_object_set(G_OBJECT(source),
		"stream-type", 0,
		"is-live", TRUE,
		"format", GST_FORMAT_TIME, NULL);
	g_signal_connect(source, "need-data", G_CALLBACK(push_data), &out_frame);
	g_signal_connect(source, "enough-data", G_CALLBACK(stop_data), NULL);

	/* Set the pipeline to "playing" state*/
	g_print("Now streaming\n");
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
}


gst_transmitter::~gst_transmitter()
{
	/* Out of the main loop, clean up nicely */
	g_print("Returned, stopping playback\n");
	gst_element_set_state(pipeline, GST_STATE_NULL);

	g_print("Deleting pipeline\n");
	gst_object_unref(GST_OBJECT(pipeline));
	g_main_loop_unref(loop);
}

void gst_transmitter::run_gst_loop()
{
	g_print("streaming...\n");
	g_main_loop_run(loop);
}

void gst_transmitter::run()
{
	std::thread t1(&gst_transmitter::run_gst_loop, this);
	t1.detach();
}

void gst_transmitter::create_h264_pipe()
{
	GstElement* encoder = gst_element_factory_make("x264enc", "h264-encoder");
	pipeline_elements.push_back(encoder);
	GstElement* encfilter = gst_element_factory_make("capsfilter", NULL);
	pipeline_elements.push_back(encfilter);
	GstElement* payloader = gst_element_factory_make("rtph264pay", NULL);
	pipeline_elements.push_back(payloader);

	GstCaps* enc_caps = gst_caps_new_simple("video/x-h264",
		"width", G_TYPE_INT, dst_frame_width,
		"height", G_TYPE_INT, dst_frame_height,
		"framerate", GST_TYPE_FRACTION, dst_framerate_hz,
		1, NULL);
	g_object_set(encfilter, "caps", enc_caps, NULL);
	g_object_set(encoder, "tune", 4, NULL);
	gst_caps_unref(enc_caps);
}

void gst_transmitter::create_h265_pipe()
{
	GstElement* encoder = gst_element_factory_make("x265enc", "h265-encoder");
	pipeline_elements.push_back(encoder);
	GstElement* encfilter = gst_element_factory_make("capsfilter", NULL);
	pipeline_elements.push_back(encoder);
	GstElement* payloader = gst_element_factory_make("rtph265pay", NULL);
	pipeline_elements.push_back(encoder);

	GstCaps* enc_caps = gst_caps_new_simple("video/x-h265",
		"width", G_TYPE_INT, dst_frame_width,
		"height", G_TYPE_INT, dst_frame_height,
		"framerate", GST_TYPE_FRACTION, dst_framerate_hz,
		1, NULL);
	g_object_set(encfilter, "caps", enc_caps, NULL);
	gst_caps_unref(enc_caps);
}

void gst_transmitter::update_last_frame(cv::Mat& frame)
{
	out_frame = &frame;
}

