#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <GL/gl.h>
#include <GL/glfw.h>

#include <pulse/pulseaudio.h>

struct action;

typedef void (*action_func)(struct action *a,float *input,float *output,uint32_t offset);

struct envelope {
	struct tick {
		enum { CMD_END, CMD_SET, CMD_LOOP } cmd;
		int pos; // in samples, negative for special stuff
		float v;
	} tick[1024];
	uint32_t idx;
	int doffset;
};

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

	struct envelope env[4];

	float input[2][4]; // double buffered
	struct action *outlet; int outletno;
} action[1024];
int action_len=0;

struct action *pickup=0;


//////////////////////////////////////////////////////////////////////////////////////////
// ACTIONS
//////////////////////////////////////////////////////////////////////////////////////////

float end;

void action_end(struct action *a, float input[4], float *output, uint32_t offset) {
	end=input[0];
}

void action_osc_sine(struct action *a, float input[4], float *output, uint32_t offset) {
	if(!output) return;
	float seconds=offset/96000.0;
	float freq=440*exp2(input[0]);
	*output+=sin(seconds*freq*2*M_PI);
}

void action_osc_square(struct action *a, float input[4], float *output, uint32_t offset) {
	if(!output) return;
	float seconds=offset/96000.0;
	float freq=440*exp2(input[0]);
	*output+=(((uint32_t)(seconds*freq))&1) ? -1 : 1;
}

void action_lowpass(struct action *a, float input[4], float *output, uint32_t offset) {
	if(!output) return;
	float freq=440*exp2(input[1]);
	float RC=1/(2*M_PI*freq);
	float dt=(1/96000.0);
	float alpha=dt/(RC+dt);
	float v=alpha*(input[0]) + (1-alpha) * a->f32;
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

float envelope_val(struct envelope *e, uint32_t offset) {
	while(e->tick[e->idx+1].pos < offset+e->doffset) {
		if(e->tick[e->idx].cmd==CMD_END) return 0;
		if(e->tick[e->idx].cmd==CMD_LOOP) { e->doffset+=-e->tick[e->idx].pos; e->idx=0; continue; }
		e->idx++;
	}
	return e->tick[e->idx].v;
}

void calculate_envelopes(int b,uint32_t offset) {
	int i;
	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];

		p->input[b][0]=envelope_val(&p->env[0],offset);
		p->input[b][1]=envelope_val(&p->env[1],offset);
		p->input[b][2]=envelope_val(&p->env[2],offset);
		p->input[b][3]=envelope_val(&p->env[3],offset);
	}
}

void execute(int b, uint32_t offset) {
	calculate_envelopes(b,offset);
	int i;

	for(i=0;i<action_len;i++) {
		struct action *p=&action[i];
		float out=0;
		def[p->def].f(p,p->input[!b],&out,offset);

		if(p->outlet) {
			p->outlet->input[b][p->outletno]+=out;
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
		data[((uint8_t)(scope[i]+127))>>5][i]=0xffffffff;
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

	int nx=a->outlet->x;
	int ny=a->outlet->y;
	nx+=16*cos(a->outletno*(M_PI/2));
	ny+=16*sin(a->outletno*(M_PI/2));

	float dx=nx-a->x;
	float dy=ny-a->y;
	float l=sqrt(dx*dx+dy*dy);
	float angle=180*(atan2(dy,dx)/M_PI);
	glRotatef(angle,0,0,1);
	glScalef(l/a->scope_width,1,1);

	float shift=(a->scope_pos%a->scope_width)/a->scope_width;

	glBegin(GL_QUADS);
	glTexCoord2f(shift,0); glVertex2i(0,-16);
	glTexCoord2f(shift+1,0); glVertex2i(1024,-2);
	glTexCoord2f(shift+1,1); glVertex2i(1024,2);
	glTexCoord2f(shift,1); glVertex2i(0,16);
	glEnd();

	glLoadIdentity();
	glTranslatef(nx,ny,-2);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);
	glVertex2i(-2,-2);
	glVertex2i( 2,-2);
	glVertex2i( 2, 2);
	glVertex2i(-2, 2);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

//////////////////////////////////////////////////////////////////////////////////////////
// VISUAL
//////////////////////////////////////////////////////////////////////////////////////////

void draw_envelope(struct envelope *e,int x,int y) {
	glLoadIdentity();
	glTranslatef(x,y,-2);

	glDisable(GL_TEXTURE_2D);
	glBegin(GL_LINE_STRIP);
	glVertex2i(0,0);
	int i; for(i=0;i<1024;i++) {
		if(e->tick[i].cmd==CMD_END) break;
		glVertex2f(e->tick[i].pos/960.0,e->tick[i].v*100);
	}
	glEnd();

	glBegin(GL_QUADS);
	for(i=0;i<1024;i++) {
		if(e->tick[i].cmd==CMD_END) break;
		float px=e->tick[i].pos/960.0,py=e->tick[i].v*100;

		switch(e->tick[i].cmd) {
		case CMD_LOOP: glColor3f(0,1,0); break;
		case CMD_SET: 
		case CMD_END: glColor3f(1,1,1);
		}

		if(i==e->idx) glColor3f(1,0,0);
		glVertex2f(px-4,py-4);
		glVertex2f(px+4,py-4);
		glVertex2f(px+4,py+4);
		glVertex2f(px-4,py+4);
	}
	glColor3f(1,1,1);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

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

	draw_envelope(action[1].env,100,500);

	glfwSwapBuffers();
}

int doexit=0;

void GLFWCALL key(int k,int a) {
	if(a==GLFW_PRESS) {
		switch(k) {
		case GLFW_KEY_ESC: doexit=1; break;
		case GLFW_KEY_SPACE:
			offset=0;
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

struct action *nearest(struct action *p,float *rd,int *no) {
	float md=INFINITY;
	struct action *m=0;
	float angle=0;

	int k; for(k=0;k<action_len;k++) {
		struct action *o=&action[k];
		if(o==p) continue;

		int dx=p->x-o->x,dy=p->y-o->y;
		float d=sqrt(dx*dx+dy*dy);
		if(d<md) {
			md=d; m=o;
			angle=atan2(-dy,-dx);
		}
	}

	*rd=md;
	int mno=2+round(angle/(M_PI/2));
	if(mno<0 || mno>3) { mno=0; }
	*no=mno;
	return m;
}

void relink(struct action *p) {
	float d;
	int no;
	struct action *n=nearest(p,&d,&no);
	if(n&&d<200) {
		p->outlet=n;
		p->outletno=no;
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
		int i;
		for(i=0;i<action_len;i++) {
			struct action *p=&action[i];
			printf("%u: (%u) %u\n",i,p->def,p->outletno);
		}
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
	action[1].outletno=0;
	action[1].x=200;
	action[1].y=200;
	action[1].scope_width=860;
	action_len++;

	action[2].def=1;
	action[2].outlet=&action[0];
	action[2].outletno=0;
	action[2].x=300;
	action[2].y=250;
	action[2].scope_width=860;

	action[1].env[0].tick[0].cmd=CMD_SET;
	action[1].env[0].tick[0].pos=0;
	action[1].env[0].tick[0].v=1;

	action[1].env[0].tick[1].cmd=CMD_SET;
	action[1].env[0].tick[1].pos=96000*1;
	action[1].env[0].tick[1].v=-1;

	action[1].env[0].tick[2].cmd=CMD_LOOP;
	action[1].env[0].tick[2].pos=96000*2;
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

