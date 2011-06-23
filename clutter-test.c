#include <clutter/clutter.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

ClutterActor *conn=0;

void connect_them(ClutterActor* a,ClutterActor *b, ClutterActor *c);
ClutterActor *connection();

void maybe_connect(ClutterActor *stage) {
	if(conn) return;
	GList *l=clutter_container_get_children(CLUTTER_CONTAINER(stage));

	GList *z=g_list_first(l);
	GList *f=g_list_next(z);
	GList *d=g_list_next(f);

	if(f&&d) {
		printf("connect!\n");
		ClutterActor *a=f->data;
		ClutterActor *b=d->data;
		conn=connection();
		connect_them(a,b,conn);
	}

	g_list_free(l);
}


ClutterActor *icon_square_wave();

void on_drag(ClutterDragAction *act,ClutterActor *a,gfloat x,gfloat y,ClutterModifierType mod,gpointer data) {
	ClutterActor *ca=icon_square_wave();
	clutter_actor_set_position (ca, x, y);
	clutter_container_add_actor (CLUTTER_CONTAINER (clutter_actor_get_parent(a)), ca);
	clutter_actor_show (ca);


	ClutterAction *da=clutter_drag_action_new();
	clutter_actor_add_action(ca,da);
	clutter_actor_set_reactive(ca,TRUE);

	clutter_drag_action_set_drag_handle(act,ca);
}


void on_drop(ClutterDragAction *act,ClutterActor *a,gfloat x,gfloat y,ClutterModifierType mod,gpointer data) {
	maybe_connect(clutter_actor_get_parent(a));
}

ClutterActor *icon_square_wave() {
  ClutterActor *ca=clutter_cairo_texture_new(32,32);
  cairo_t *c=clutter_cairo_texture_create(CLUTTER_CAIRO_TEXTURE (ca));

  cairo_set_source_rgb(c,1,1,1);
  cairo_move_to(c,10,20);
  cairo_line_to(c,13,20);
  cairo_line_to(c,13,12);
  cairo_line_to(c,19,12);
  cairo_line_to(c,19,20);
  cairo_line_to(c,22,20);
  cairo_stroke(c);

  cairo_set_source_rgba(c,1,1,1,0.5);
  cairo_arc(c,16,16,14,0,M_PI*2);
  cairo_stroke(c);
  cairo_destroy (c);

  clutter_texture_set_filter_quality(CLUTTER_TEXTURE(ca),CLUTTER_TEXTURE_QUALITY_HIGH);
  clutter_actor_set_size (ca, 32, 32);


  return ca;
}

void connect_them(ClutterActor* a,ClutterActor *b, ClutterActor *c) {
	float ax,ay,bx,by;
	float aw,ah,bw,bh;
	clutter_actor_get_position(a,&ax,&ay);
	clutter_actor_get_position(b,&bx,&by);

	clutter_actor_get_size(a,&aw,&ah);
	clutter_actor_get_size(b,&bw,&bh);

	clutter_actor_set_position(c,ax+aw/2,ay+ah/2);

	float sy=by-ay,sx=bx-ax;

	float length=sqrt(sy*sy+sx*sx);
	clutter_actor_set_size(c,length,16);

	float angle=180*(atan2(sy,sx)/M_PI);
	clutter_actor_set_anchor_point(c,0,ah/2-1);
	clutter_actor_set_rotation(c,CLUTTER_Z_AXIS,angle,0,0,0);

	clutter_container_add_actor (CLUTTER_CONTAINER (clutter_actor_get_parent(a)), c);
	clutter_actor_show (c);
}

#define CONNWIDTH 512
#define CONNCOLOR 0x40ffff00

ClutterActor *connection() {
  ClutterActor *ca=clutter_texture_new();
  clutter_texture_set_filter_quality(CLUTTER_TEXTURE(ca),CLUTTER_TEXTURE_QUALITY_HIGH);
  clutter_actor_set_size (ca,CONNWIDTH,16);
  clutter_actor_set_position(ca,100,100);

  uint32_t data[16][CONNWIDTH];

  memset(data,0,sizeof(data));
  int i;for(i=0;i<CONNWIDTH;i++) {
    float f=0; //sin(i/10.0);
    int d=7*f;

    if(d==0) {
    	data[8][i]=CONNCOLOR;
    } else if(d>0) {
      int steps=d; if(steps>7) steps=7;
      for(;steps>=0;steps--) data[8+steps][i]=CONNCOLOR;
    } else { // d<0
      int steps=-d; if(steps>7) steps=7;
      for(;steps>=0;steps--) data[7-steps][i]=CONNCOLOR;
    }
  }

  clutter_texture_set_from_rgb_data(CLUTTER_TEXTURE(ca),(guchar*)data,TRUE,CONNWIDTH,16,CONNWIDTH*4,4,CLUTTER_TEXTURE_NONE,0);

  return ca;
}


int main(int argc, char *argv[])
{
  ClutterColor stage_color = { 0x10, 0x20, 0x20, 0xff };

  clutter_init (&argc, &argv);

  /* Get the stage and set its size and color: */
  ClutterActor *stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 200, 200);
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

  ClutterActor *ca=icon_square_wave();
  clutter_actor_set_position (ca, 10, 10);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), ca);
  clutter_actor_show (ca);

  ClutterAction *da=clutter_drag_action_new();
  clutter_actor_add_action(ca,da);
  clutter_actor_set_reactive(ca,TRUE);
  g_signal_connect(da, "drag-begin",G_CALLBACK(on_drag),NULL);
  g_signal_connect(da, "drag-end",G_CALLBACK(on_drop),NULL);


  /* Show the stage: */
  clutter_actor_show (stage);

  /* Start the main loop, so we can respond to events: */
  clutter_main ();

  return EXIT_SUCCESS;

}
