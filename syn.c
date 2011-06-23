#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <pulse/pulseaudio.h>

struct action;

typedef void (*action_func)(struct action *a,float *input,float *output,uint32_t offset);

struct action {
	action_func f;
	union {
		void *p;
		uint8_t u8;
		uint32_t u32;
		float f32;
	};

	float input[2]; // double buffered
	struct action *outlet;
} action[1024];
int action_len=0;


//////////////////////////////////////////////////////////////////////////////////////////
// ACTIONS
//////////////////////////////////////////////////////////////////////////////////////////

void action_end(struct action *a, float *input, float *output, uint32_t offset) {
	// just for storing inputs
}

void action_osc_sine(struct action *a, float *input, float *output, uint32_t offset) {
	float seconds=offset/96000.0;
	*output=sin(seconds * 440 * 2*M_PI);
}

void action_osc_square(struct action *a, float *input, float *output, uint32_t offset) {
	float seconds=offset/96000.0;
	*output=(((uint32_t)(seconds*440))&1) ? -1 : 1;
}

void action_lowpass(struct action *a, float *input, float *output, uint32_t offset) {
	float RC=1/(2*M_PI*440*4);
	float dt=(1/96000.0);
	float alpha=dt/(RC+dt);
	*output = alpha*(*input) + (1-alpha) * a->f32;
	a->f32=*output;
}

//////////////////////////////////////////////////////////////////////////////////////////
// EXECUTION
//////////////////////////////////////////////////////////////////////////////////////////

void execute(int b, uint32_t offset) {
	int i;
	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		p->f(p,&p->input[b],p->outlet?&p->outlet->input[b]:0,offset);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
// AUDIO OUTPUT
//////////////////////////////////////////////////////////////////////////////////////////

static void pa_state_cb(pa_context *c, void *userdata) {
	pa_context_state_t state=pa_context_get_state(c);
	int *pa_ready=userdata;
	switch  (state) {
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED: *pa_ready=2; break;
	case PA_CONTEXT_READY: *pa_ready=1; break;
	default:;
	}
}

int offset=0;

static void audio_request_cb(pa_stream *s, size_t length, void *userdata) {
	int i;
	uint32_t buf[length/4];

	if(pa_stream_is_corked(s)) return;

	for(i=0;i<length/4;i++) {
		int b=(offset+i)&1;
		execute(b,offset+i);
		buf[i]=0x7fff * action[0].input[b];
	}
	offset+=i;
	pa_stream_write(s,buf,length,0,0,PA_SEEK_RELATIVE);
}

static void audio_underflow_cb(pa_stream *s, void *userdata) {
	printf("underflow\n");
}


pa_stream *ps;

void audio_init() {
	pa_threaded_mainloop *pa_ml=pa_threaded_mainloop_new();
	pa_mainloop_api *pa_mlapi=pa_threaded_mainloop_get_api(pa_ml);
	pa_context *pa_ctx=pa_context_new(pa_mlapi, "te");
	pa_context_connect(pa_ctx, NULL, 0, NULL);
	int pa_ready = 0;
	pa_context_set_state_callback(pa_ctx, pa_state_cb, &pa_ready);

	pa_threaded_mainloop_start(pa_ml);
	while(pa_ready==0) { ; }

	printf("audio ready\n");

	if (pa_ready == 2) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
		pa_threaded_mainloop_free(pa_ml);
	}

	pa_sample_spec ss;
	ss.rate=96000;
	ss.channels=1;
	ss.format=PA_SAMPLE_S24_32LE;
	ps=pa_stream_new(pa_ctx,"Playback",&ss,NULL);
	pa_stream_set_write_callback(ps,audio_request_cb,NULL);
	pa_stream_set_underflow_callback(ps,audio_underflow_cb,NULL);

	pa_buffer_attr bufattr;
	bufattr.fragsize = (uint32_t)-1;
	bufattr.maxlength = pa_usec_to_bytes(20000,&ss);
	bufattr.minreq = pa_usec_to_bytes(0,&ss);
	bufattr.prebuf = 0;
	bufattr.tlength = pa_usec_to_bytes(20000,&ss);

	pa_stream_connect_playback(ps,NULL,&bufattr,
		PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_ADJUST_LATENCY|PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_START_CORKED,NULL,NULL);
}

//////////////////////////////////////////////////////////////////////////////////////////
// COMMANDS
//////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////
// VISUAL
//////////////////////////////////////////////////////////////////////////////////////////

static gboolean on_expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
	cairo_t *cr = gdk_cairo_create (widget->window);
	cairo_destroy(cr);
	return FALSE;
}

GtkWidget *window;

gboolean update_view(gpointer _) {
	gdk_window_invalidate_rect(gtk_widget_get_window(window),0,1);

	return FALSE;
}

static gboolean on_keypress(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	offset=0; pa_stream_cork(ps,0,0,0);

	switch(event->keyval) {
        case GDK_KEY_Escape: gtk_main_quit(); break;
	}

	return FALSE;
}

int main(int argc,char *argv[])
{
	g_thread_init(0);

	action[0].f=action_end;
	action_len++;


	action[1].f=action_osc_square;
	action[1].outlet=&action[2];
	action_len++;

	action[2].f=action_lowpass;
	action[2].outlet=&action[0];
	action_len++;

	gtk_init(&argc,&argv);
	window=gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW (window),"te");

	g_signal_connect(window,"destroy",G_CALLBACK (gtk_main_quit),NULL);
	gtk_widget_add_events(window,GDK_KEY_PRESS_MASK);

        g_signal_connect(window,"key-press-event",G_CALLBACK(on_keypress),NULL);

	GtkWidget *a=gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window),a);
	g_signal_connect(a,"expose-event",G_CALLBACK(on_expose_event),NULL);
	gtk_widget_show_all(window);

	audio_init();

	gtk_main();
	
	return 0;
}

