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
	int x,y,def;

	union {
		void *p;
		uint8_t u8;
		uint32_t u32;
		float f32;
	};

	int8_t scope[1024];
	int scope_pos;
	int scope_width;

	float input[2]; // double buffered
	struct action *outlet;
} action[1024];
int action_len=0;

struct action *pickup=0;


//////////////////////////////////////////////////////////////////////////////////////////
// ACTIONS
//////////////////////////////////////////////////////////////////////////////////////////

float end;

void action_end(struct action *a, float *input, float *output, uint32_t offset) {
	end=*input;
}

void action_osc_sine(struct action *a, float *input, float *output, uint32_t offset) {
	if(!output) return;
	float seconds=offset/96000.0;
	float freq=440+exp2(*input);
	*output+=sin(seconds*freq*2*M_PI);
}

void action_osc_square(struct action *a, float *input, float *output, uint32_t offset) {
	if(!output) return;
	float seconds=offset/96000.0;
	float freq=440+exp2(*input);
	*output+=(((uint32_t)(seconds*freq))&1) ? -1 : 1;
}

void action_lowpass(struct action *a, float *input, float *output, uint32_t offset) {
	if(!output) return;
	float freq=440+exp2(*input);
	float RC=1/(2*M_PI*freq);
	float dt=(1/96000.0);
	float alpha=dt/(RC+dt);
	float v=alpha*(*input) + (1-alpha) * a->f32;
	*output+=v;
	a->f32=v;
}

struct def {
	action_func f;
} def[] = {
		{action_osc_square},
		{action_lowpass},
		{action_end},
		{action_osc_sine},
	};

const int deflen=sizeof(def)/sizeof(*def);

//////////////////////////////////////////////////////////////////////////////////////////
// EXECUTION
//////////////////////////////////////////////////////////////////////////////////////////

void execute(int b, uint32_t offset) {
	int i;
	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		p->input[b]=0;
	}

	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		float out=0;
		def[p->def].f(p,&p->input[!b],&out,offset);

		if(p->outlet) {
			p->outlet->input[b]+=out;
			p->scope[(p->scope_pos++)%p->scope_width]=127*out;
		}
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
		buf[i]=0x7fff * end;
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

GLuint scope_id;
void make_scope(int8_t scope[1024],int scope_width) {
	uint32_t data[16][1024];
	int i;
	memset(data,0,sizeof(data));
	for(i=0;i<scope_width;i++) {
		data[((uint8_t)(scope[i]+127))>>4][i]=0xffffffff;
	}

	glBindTexture(GL_TEXTURE_2D,scope_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1024,16,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
}

void draw_scope(struct action *a) {
	if(!a->outlet) return;
	make_scope(a->scope,a->scope_width);

	glLoadIdentity();
	glTranslatef(a->x,a->y,-2);

	float dx=a->outlet->x-a->x;
	float dy=a->outlet->y-a->y;
	float l=sqrt(dx*dx+dy*dy);
	float angle=180*(atan2(dy,dx)/M_PI);
	glRotatef(angle,0,0,1);
	glScalef(l/a->scope_width,1,1);

	float shift=(a->scope_pos%a->scope_width)/a->scope_width;

	glBegin(GL_QUADS);
	glTexCoord2f(shift,0); glVertex2i(0,-8);
	glTexCoord2f(shift+1,0); glVertex2i(1024,-2);
	glTexCoord2f(shift+1,1); glVertex2i(1024,2);
	glTexCoord2f(shift,1); glVertex2i(0,8);
	glEnd();
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

void draw_connection(int ax,int ay,int bx, int by) {
	glLoadIdentity();
	glTranslatef(0,0,-2);
	glDisable(GL_TEXTURE_2D);
	glColor3f(1,1,1);
	glBegin(GL_LINES);
	glVertex2i(ax,ay);
	glVertex2i(bx,by);
	glEnd();
	glEnable(GL_TEXTURE_2D);

}


void draw_bar() {
	int i;
	for(i=0;i<deflen;i++) {
		draw_icon(i,32+48*i,32,0);
	}
}


void draw() {
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	int i;
	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		draw_bar();
		draw_icon(p->def,p->x,p->y,0);
		draw_scope(p);
	}

	glfwSwapBuffers();
}

int doexit=0;

void GLFWCALL key(int k,int a) {
	if(a==GLFW_PRESS) {
		switch(k) {
		case GLFW_KEY_ESC: doexit=1; break;
		case GLFW_KEY_SPACE:
			pa_stream_cork(ps,!pa_stream_is_corked(ps),0,0);
			printf("cork\n");
			
			break;
		}
	}
}


struct action *action_at(int x,int y) {
	int i;
	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		int dx=p->x-x,dy=p->y-y;
		if(sqrt(dx*dx+dy*dy)<16) { return p; }
	}
	return 0;
}

struct action *nearest(struct action *p,float *rd) {
	float md=INFINITY;
	struct action *m=0;

	int k; for(k=0;k<action_len;k++) {
		struct action *o=&action[k];
		if(o==p) continue;

		int dx=p->x-o->x,dy=p->y-o->y;
		float d=sqrt(dx*dx+dy*dy);
		if(d<md) { md=d; m=o; }
	}

	*rd=md;
	return m;
}

void relink(struct action *p) {
	float d;
	struct action *n=nearest(p,&d);
	if(n&&d<200) {
		p->outlet=n;
	} else {
		p->outlet=0;
	}
}

void GLFWCALL button(int b,int act) {
	int x,y;
	glfwGetMousePos(&x,&y);
	if(act==GLFW_PRESS) {
		if(y<48) {
			unsigned int i=(x-16)/48;
			if(i>=deflen) return;

			memset(&action[action_len],0,sizeof(action[action_len]));
			action[action_len].def=i;
			action[action_len].scope_width=860;
			pickup=&action[action_len];
			action_len++;
			
		} else {
			pickup=action_at(x,y);
		}
	} else {
		pickup=0;
	}
}

void GLFWCALL mouse(int x,int y) {
	if(pickup) { pickup->x=x; pickup->y=y; relink(pickup); }
}

double prectime() {
	struct timeval t;
	gettimeofday(&t,0);
	return t.tv_sec+t.tv_usec/1e6;
}

int main(int argc,char *argv[])
{
	memset(action,0,sizeof(action));

	action[0].def=2;
	action[0].x=100;
	action[0].y=100;
	action[0].scope_width=256;
	action_len++;

	action[1].def=0;
	action[1].outlet=&action[2];
	action[1].x=200;
	action[1].y=200;
	action[1].scope_width=860;
	action_len++;

	action[2].def=1;
	action[2].outlet=&action[0];
	action[2].x=300;
	action[2].y=250;
	action[2].scope_width=860;
	action_len++;

	audio_init();


	glfwInit();
	glfwOpenWindow(1024,760,8,8,8,8,8,8,GLFW_WINDOW);
	glfwSetMousePosCallback(mouse);
	glfwSetMouseButtonCallback(button);
	glfwSetKeyCallback(key);

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
	glGenTextures(1, &scope_id);

	for(;!doexit;) {
		double pt=prectime();
		float dx=2*sin(pt/M_PI);
		float dy=2*sin(pt/M_PI);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(dx,1024+dx,dy+760,dy,1,10);
		glMatrixMode(GL_MODELVIEW);

		draw();
	}
	glfwTerminate();

	return 0;
}

