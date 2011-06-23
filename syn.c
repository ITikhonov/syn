#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <GL/gl.h>
#include <GL/glfw.h>

#include <pulse/pulseaudio.h>

struct action;

typedef void (*action_func)(struct action *a,float *input,float *output,uint32_t offset);

struct action {
	int x,y;

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


pa_threaded_mainloop *pa_ml;
pa_stream *ps;

void audio_init() {
	pa_ml=pa_threaded_mainloop_new();
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
// Images
//////////////////////////////////////////////////////////////////////////////////////////

GLuint load(char *name,int w,int h) {
	int size=w*h*4;
	char buf[size];
	FILE *f=fopen(name,"r");
	fread(buf,size,1,f);
	fclose(f);

	GLuint id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D,id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,buf);
	return id;
}

//////////////////////////////////////////////////////////////////////////////////////////
// VISUAL
//////////////////////////////////////////////////////////////////////////////////////////

GLuint icons=-1;

void draw_icon(int i,int x,int y, float a) {
	glLoadIdentity();
	glTranslatef(x,y,-2);
	glRotatef(a,0,0,1);

	float pz=(1/4.0);
	float tz=i*pz;

	glBindTexture(GL_TEXTURE_2D,icons);
	glBegin(GL_QUADS);
	glTexCoord2f(tz,0); glVertex2i(-16,-16);
	glTexCoord2f(tz,1); glVertex2i(-16,16);
	glTexCoord2f(tz+pz,1); glVertex2i(16,16);
	glTexCoord2f(tz+pz,0); glVertex2i(16,-16);
	glEnd();
}

void draw() {
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	draw_icon(0,100,100,0);
	draw_icon(1,200,200,0);
	draw_icon(2,300,200,0);

	glfwSwapBuffers();
}

int doexit=0;

void GLFWCALL key(int k,int a) {
	if(a==GLFW_PRESS) {
		switch(k) {
		case GLFW_KEY_ESC: doexit=1; break;
		}
	}
}

void GLFWCALL mouse(int x,int y) {
}

int main(int argc,char *argv[])
{
	action[0].f=action_end;
	action[0].x=100;
	action[0].y=100;
	action_len++;

	action[1].f=action_osc_square;
	action[1].outlet=&action[2];
	action[1].x=200;
	action[1].y=200;
	action_len++;

	action[2].f=action_lowpass;
	action[2].outlet=&action[0];
	action[2].x=300;
	action[2].y=300;
	action_len++;

	audio_init();


	glfwInit();
	glfwOpenWindow(1024,760,8,8,8,8,8,8,GLFW_WINDOW);
	glfwSetMousePosCallback(mouse);
	glfwSetKeyCallback(key);

	glViewport(0,0,1024,760);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,1024,760,0,1,10);
	glMatrixMode(GL_MODELVIEW);
	glClearColor(0.06f,0.12f,0.12f,0.0f);
	glEnable(GL_TEXTURE_2D);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glEnable(GL_BLEND); 
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	icons=load("icons.rgba",128,32);

	for(;!doexit;) {
		draw();
	}
	glfwTerminate();

	return 0;
}

