#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <lcm/lcm-cpp.hpp>
#include <vector>
#include <deque>
#include <iostream>
#include <string>
#include "math/point.hpp"
// core api
#include "vx/vx.h"
#include "vx/vx_util.h"
#include "vx/gtk/vx_gtk_display_source.h"

// drawables
#include "vx/vxo_drawables.h"

//common
#include "common/getopt.h"
#include "common/timestamp.h"
#include "common/zarray.h"

//imagesource
#include "imagesource/image_source.h"
#include "imagesource/image_convert.h"
#include "imagesource/image_u32.h"
#include "imagesource/image_util.h"

//A2
#include "image_processor.hpp"
#include "hsv.hpp"

class state_t
{
    public:
        // vx stuff
        bool                running;
        getopt_t            *gopt;
        char                *pic_url;
        image_processor     im_processor;
        std::deque<max_min_hsv> hsv_ranges;
        std::deque<uint32_t> color_selected;
        eecs467::Point<float>   click_point;
        bool                is_new_selection;
        vx_application_t    vxapp;
        vx_world_t          *vxworld;
        zhash_t             *layers;
        vx_event_handler_t  *vxeh;
        vx_mouse_event_t    last_mouse_event;
        vx_gtk_display_source_t* appwrap;
        pthread_mutex_t     data_mutex; // shared data lock
        pthread_mutex_t     mutex;
        pthread_t animate_thread;

    public:
        state_t()
        {
            //GUI init stuff
            vxworld = vx_world_create();
            printf("create world\n");
            vxeh = (vx_event_handler_t*) calloc(1,sizeof(*vxeh));
            vxeh->mouse_event = mouse_event;
            vxeh->touch_event = touch_event;
            vxeh->key_event = key_event;
            vxeh->dispatch_order = 100;
            vxeh->impl = this;
            printf("create vxeh\n");
            vxapp.impl= this;
            vxapp.display_started = display_started;
            vxapp.display_finished = display_finished;
            layers = zhash_create(sizeof(vx_display_t*), sizeof(vx_layer_t*), zhash_ptr_hash, zhash_ptr_equals);
            pthread_mutex_init (&mutex, NULL);
            pthread_mutex_init (&data_mutex,NULL);
            running = true;
            is_new_selection = false;
            gopt = getopt_create(); 
            click_point.x = -1;
            click_point.y = -1;
        }

        ~state_t()
        {
            vx_world_destroy(vxworld);
            if(zhash_size(layers) != 0){
                zhash_destroy(layers);
            }
            if(gopt){
                getopt_destroy(gopt);
            }
            pthread_mutex_destroy(&mutex);
        }

        void init_thread()
        {
            pthread_create(&animate_thread,NULL,&state_t::render_loop,this);
        }

        static int mouse_event (vx_event_handler_t *vxeh, vx_layer_t *vl, vx_camera_pos_t *pos, vx_mouse_event_t *mouse)
        {
            state_t *state = (state_t*)vxeh->impl;
            pthread_mutex_lock(&state->data_mutex);
            // vx_camera_pos_t contains camera location, field of view, etc
            // vx_mouse_event_t contains scroll, x/y, and button click events
            if ((mouse->button_mask & VX_BUTTON1_MASK) &&
                    !(state->last_mouse_event.button_mask & VX_BUTTON1_MASK)) {
                //state->click_point.x = mouse->x;
                //state->click_point.y = mouse->y;
                vx_ray3_t ray;
                vx_camera_pos_compute_ray (pos, mouse->x, mouse->y, &ray);
                double ground[3];
                vx_ray3_intersect_xy (&ray, 0, ground);
                printf ("Mouse clicked at coords: [%8.3f, %8.3f] Ground clicked at coords: [%6.3f, %6.3f]\n",
                        mouse->x, mouse->y, ground[0], ground[1]);
                state->click_point.x = ground[0];
                state->click_point.y = ground[1];
                state->is_new_selection = true;
            }
            state->last_mouse_event = *mouse;
            pthread_mutex_unlock(&state->data_mutex);
            return 0;
        }

        static int key_event (vx_event_handler_t *vxeh, vx_layer_t *vl, vx_key_event_t *key)
        {
            state_t *state = (state_t*)vxeh->impl;
            pthread_mutex_lock(&state->data_mutex);
            if(!key->released){
                if(key->key_code == 'S' || key->key_code == 's'){
                    //save hsv
                    FILE *fp = fopen("hsv_range.txt","w");
                    max_min_hsv hsv_tmp = state->hsv_ranges.back();
                    hsv_color_t max_hsv = hsv_tmp.get_max_HSV();
                    hsv_color_t min_hsv = hsv_tmp.get_min_HSV();
                    fprintf(fp,"%f %f %f %f %f %f\n",min_hsv.H,max_hsv.H,min_hsv.S,max_hsv.S,min_hsv.V,max_hsv.V);
                    fclose(fp);
                }
                else if(key->key_code == VX_KEY_DEL){
                    if(!state->color_selected.empty() && !state->hsv_ranges.empty()){
                        state->color_selected.pop_back();
                        state->hsv_ranges.pop_back();
                    }
                }
            }
            pthread_mutex_unlock(&state->data_mutex); 
            return 0;
        }

        static int touch_event (vx_event_handler_t *vh, vx_layer_t *vl, vx_camera_pos_t *pos, vx_touch_event_t *mouse)
        {
            return 0; // Does nothing
        }

        static void* render_loop(void* data)
        {
            state_t * state = (state_t*) data;
            int fps = 60; 
            while (state->running) {
                pthread_mutex_lock(&state->data_mutex);
                vx_buffer_t *buf = vx_world_get_buffer(state->vxworld,"image");
                image_u32_t *im; 
                im = image_u32_create_from_pnm(state->pic_url);
                if(state->is_new_selection && state->click_point.x != -1 && state->click_point.y != -1){
                    max_min_hsv tmp_hsv;
                    if(state->hsv_ranges.empty()){
                        tmp_hsv = max_min_hsv();
                    }
                    else{
                        tmp_hsv = state->hsv_ranges.back();
                    }
                    int x = (int)(state->click_point.x+320);
                    int y = (int)(state->click_point.y+240);
                    uint32_t curr_c = im->buf[y*im->stride + x]; 
                    state->color_selected.push_back(curr_c);
                    tmp_hsv.updateHSV(state->im_processor.rgb_to_hsv(curr_c));
                    state->is_new_selection = false;
                    state->hsv_ranges.push_back(tmp_hsv);
                } 

                if(!state->hsv_ranges.empty()){
                    state->im_processor.image_select(im,state->hsv_ranges.back());
                }
                if(im != NULL){
                    vx_object_t *vim = vxo_image_from_u32(im,
                            VXO_IMAGE_FLIPY,
                            VX_TEX_MIN_FILTER | VX_TEX_MAG_FILTER);
                    //use pix coords to make a fix image
                    vx_buffer_add_back (buf,vxo_chain (
                                vxo_mat_translate3 (-im->width/2., -im->height/2., 0.),
                                vim));
                    image_u32_destroy (im);
                }
                pthread_mutex_unlock(&state->data_mutex);
                vx_buffer_swap(buf);
                usleep(1000000/fps);
            }
            return NULL;
        }

        static void display_finished(vx_application_t * app, vx_display_t * disp)
        {
            state_t * state = (state_t*)app->impl;
            pthread_mutex_lock(&state->mutex);

            vx_layer_t * layer = NULL;

            // store a reference to the world and layer that we associate with each vx_display_t
            zhash_remove(state->layers, &disp, NULL, &layer);

            vx_layer_destroy(layer);

            pthread_mutex_unlock(&state->mutex);
        }

        static void display_started(vx_application_t * app, vx_display_t * disp)
        {
            state_t * state = (state_t*)app->impl;

            vx_layer_t * layer = vx_layer_create(state->vxworld);
            vx_layer_set_display(layer, disp);
            //important setting for the handler
            if(state->vxeh != NULL){
                vx_layer_add_event_handler(layer,state->vxeh);
            }
            pthread_mutex_lock(&state->mutex);
            // store a reference to the world and layer that we associate with each vx_display_t
            zhash_put(state->layers, &disp, &layer, NULL, NULL);
            pthread_mutex_unlock(&state->mutex);
        }

};

int main(int argc, char ** argv)
{
    state_t state;
    printf("init state\n");
    //find camera urls and use the first one
    getopt_add_string (state.gopt, 'f', "picurl", "", "Picture URL");

    if (!getopt_parse (state.gopt, argc, argv, 1)) {
        printf ("Usage: %s [--url=CAMERAURL] [other options]\n\n", argv[0]);
        getopt_do_usage (state.gopt);
        exit (EXIT_FAILURE);
    }

    if (strncmp (getopt_get_string (state.gopt, "picurl"), "", 1)) {
        state.pic_url = strdup (getopt_get_string (state.gopt, "picurl"));
        printf ("URL: %s\n", state.pic_url);
    }
    else{
        printf("no pic URL provided, ABORT\n");
        exit(EXIT_FAILURE);
    }
    state.init_thread();
    //vx
    gdk_threads_init();
    gdk_threads_enter();
    gtk_init(&argc, &argv);

    state.appwrap = vx_gtk_display_source_create(&state.vxapp);
    GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget * canvas = vx_gtk_display_source_get_widget(state.appwrap);
    gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
    gtk_container_add(GTK_CONTAINER(window), canvas);
    gtk_widget_show (window);
    gtk_widget_show (canvas); // XXX Show all causes errors!
    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_main(); // Blocks as long as GTK window is open

    gdk_threads_leave();
    vx_gtk_display_source_destroy(state.appwrap);
    //vx
    state.running = 0;
    pthread_join(state.animate_thread,NULL);

    vx_global_destroy();
}
