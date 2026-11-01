/* Actual native menu adapter plus portable state/renderer. No camera access.
 * Stubs model the pinned T6 key constants and ordinary menu services. */
#include "dryos.h"
#include "menu.h"
#include "bmp.h"
#include "gui.h"
#include "gui-common.h"
#include "lens.h"
#include <stdarg.h>
#include <limits.h>
#include "native_format.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"retro FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
int ml_started=1,ml_shutdown_requested,mock_display_on=1,lv,shooting_mode;
struct mock_lens_info lens_info;
static int menu_on=1,tab_on=1,entry_on=1,movie,half,stops,draws,acks,av_reads,av_status;
static int end_poll;
static int queued_toggles, native_half_signals, redraw_flood_stopped;
unsigned get_ms_clock(void){return 1000;}
void msleep(int ms){(void)ms;if(end_poll)ml_shutdown_requested=1;}
void *fio_malloc(size_t n){return malloc(n);}
void fio_free(void *p){free(p);}
size_t read_file(const char *p,void *dst,size_t n){(void)p;(void)dst;(void)n;return 0;}
int get_halfshutter_pressed(void){return half;}
int is_movie_mode(void){return movie;}
void menu_redraw(void){}
void gui_stop_menu(void){if(menu_on){stops++;queued_toggles++;}}
int gui_menu_shown(void){return menu_on;}
int is_menu_active(char *n){(void)n;return tab_on;}
int is_menu_entry_selected(char *m,char *n){(void)m;(void)n;return entry_on;}
void keyrepeat_ack(int key){(void)key;acks++;}
void menu_add(char *n,struct menu_entry *e,int count){(void)n;(void)e;(void)count;}
int update_bgmt_av_status(struct event *e){CHECK(e && e->obj);av_reads++;return av_status;}
void bmp_fill(uint8_t color,int x,int y,int w,int h){(void)color;CHECK(x>=0&&y>=0&&w>0&&h>0&&x+w<=720&&y+h<=480);draws++;}
int bmp_printf(uint32_t font,int x,int y,const char *fmt,...){char b[128];va_list ap;CHECK(((font>>8)&255u)==NO_BG_ERASE);CHECK(t6_native_format_allowed(fmt));CHECK(x>=0&&y>=0&&x<720&&y<480);va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);CHECK(strlen(b)<=72);draws++;return 0;}
#define snprintf t6_mock_snprintf
#include "../../../integrations/magiclantern/1300D.110/src/film_menu.c"
#undef snprintf

static void reset(void){
 memset(&retro,0,sizeof retro);retro_handoff_event=NULL;retro_av_handoff=0;
 menu_on=tab_on=entry_on=mock_display_on=1;movie=half=lv=ml_shutdown_requested=0;
 lens_info.job_state=0;film_auto=film_request=film_bench_request=film_cancel=film_processing=0;
 film_count=3;film_stock=0;film_effect=0;film_amber=0;
 snprintf(film_names[0],32,"PORTRA");snprintf(film_names[1],32,"SOUP 800");
 stops=draws=acks=av_reads=end_poll=0;
 queued_toggles=native_half_signals=redraw_flood_stopped=0;
}
static int key(int code){struct event e={0,code,NULL,0};return film_retro_event_gate(&e);}
static void open_ui(void){film_retro_open_menu(NULL,1);CHECK(retro.visible);}
/* The relevant pinned menu.c path, including our menu-only passthrough gate.
 * Lines5255-5259 give gui_sem for half-press; lines5777-5788 consume it as
 * a TOGGLE. Queue both potential producers before the menu task runs: the
 * old adapter close plus native close would reopen the menu after two signals. */
static void modeled_native_menu_handler(struct event *e){
 if(!menu_on||film_retro_menu_passthrough(e))return;
 if(e->param==BGMT_PRESS_HALFSHUTTER){redraw_flood_stopped=1;native_half_signals++;queued_toggles++;}
}
static void modeled_chain(int code){
 struct event e={0,code,NULL,0};
 if(film_retro_event_gate(&e)!=FILM_RETRO_CONSUME)modeled_native_menu_handler(&e);
}
static void drain_menu_task(void){while(queued_toggles){menu_on=!menu_on;queued_toggles--;}}
static void pure_state_tests(void){
 struct film_retro_state s={0};struct film_retro_settings p={0,INT_MAX,INT_MAX,1,1};unsigned a;int i;
 film_retro_open(&s);
 CHECK(film_retro_step(&s,&p,FILM_KEY_DIAL_NEXT,1,0,&a)==0&&p.stock==0);
 p.stock_count=16;p.stock=15;
 film_retro_step(&s,&p,FILM_KEY_DIAL_NEXT,1,0,&a);CHECK(p.stock==0);
 film_retro_step(&s,&p,FILM_KEY_DIAL_PREV,1,0,&a);CHECK(p.stock==15);
 film_retro_step(&s,&p,FILM_KEY_DOWN,1,0,&a);
 for(i=0;i<10001;i++)film_retro_step(&s,&p,FILM_KEY_DIAL_NEXT,1,0,&a);
 CHECK(p.effect>=0&&p.effect<=3);
 film_retro_step(&s,&p,FILM_KEY_Q,1,0,&a);CHECK(s.page==1&&s.selected==0);
 film_retro_step(&s,&p,FILM_KEY_DOWN,1,0,&a);
 film_retro_step(&s,&p,FILM_KEY_SET,1,1,&a);CHECK(!(a&FILM_RETRO_DEVELOP));
 film_retro_step(&s,&p,FILM_KEY_DOWN,1,1,&a);
 film_retro_step(&s,&p,FILM_KEY_SET,1,1,&a);CHECK((a&FILM_RETRO_CANCEL)&&!p.auto_on);
 CHECK(film_retro_step(&s,&p,FILM_KEY_SYSTEM,1,0,&a)==1&&s.visible&&!a);
}
int main(void){
 int i;struct event e;char val[35];struct menu_display_info info={val,1,0};
 const int presses[]={BGMT_PRESS_UP,BGMT_PRESS_DOWN,BGMT_PRESS_LEFT,BGMT_PRESS_RIGHT,BGMT_PRESS_SET,BGMT_PRESS_DISP};
 const int releases[]={BGMT_UNPRESS_UP,BGMT_UNPRESS_DOWN,BGMT_UNPRESS_LEFT,BGMT_UNPRESS_RIGHT,BGMT_UNPRESS_SET,BGMT_UNPRESS_DISP};
 const int native[]={BGMT_PLAY,BGMT_LV,BGMT_ISO,BGMT_PRESS_ZOOM_IN,BGMT_UNPRESS_ZOOM_IN,BGMT_PRESS_ZOOM_OUT,BGMT_UNPRESS_ZOOM_OUT,BGMT_PRESS_HALFSHUTTER,BGMT_UNPRESS_HALFSHUTTER,BGMT_PRESS_FULLSHUTTER,BGMT_UNPRESS_FULLSHUTTER};
 pure_state_tests();reset();CHECK(film_retro_event_gate(NULL)==1);
 CHECK(COUNT(film_menu)==9&&!strcmp(film_menu[7].name,"Retro dashboard")&&
       !strcmp(film_menu[8].name,"CPU limits test"));
 /* CPU action coalesces one request, disables Auto and drops Develop.
  * Busy presses cannot clear an active cancellation or queue another run. */
 film_auto=film_request=film_cancel=1;film_cpu_limits(NULL,1);
 CHECK(film_bench_request&&!film_auto&&!film_request&&!film_cancel&&
       !strcmp(film_status,"CPU test queued; Auto OFF"));
 film_cpu_limits(NULL,1);CHECK(film_bench_request==1);
 film_auto=film_request=1;
 CHECK(film_t6_bench_pending()&&film_t6_take_bench_request()==1&&
       !film_t6_bench_pending()&&!film_auto&&!film_request);
 CHECK(!film_t6_take_bench_request());
 film_processing=film_auto=film_request=film_cancel=1;film_cpu_limits(NULL,1);
 CHECK(!film_bench_request&&film_processing&&film_auto&&film_request&&film_cancel);
 film_request=0;film_develop_latest(NULL,1);
 CHECK(film_processing&&film_cancel&&!film_request);
 film_processing=0;film_bench_request=1;film_stop(NULL,1);
 CHECK(!film_bench_request&&!film_auto&&!film_request&&film_cancel);
 reset();
 CHECK(!t6_native_format_allowed("%.34s")&&!t6_native_format_allowed("%.31s"));
 CHECK(!t6_native_format_allowed("%.*s")&&t6_native_format_allowed("%s/%s%04d.CR2"));
 /* Exact filenames and clipped visible text must survive the camera subset. */
 {struct film_t6_settings settings;char expected[35];
  snprintf(film_files[0],32,"portra.fs");film_t6_snapshot(&settings);CHECK(!strcmp(settings.stock,"portra.fs"));
  memset(film_files[0],'X',32);film_t6_snapshot(&settings);CHECK(strlen(settings.stock)==31&&settings.stock[0]=='X');
  film_t6_set_status("A long status string must clip as text, never become a precision format");
  memcpy(expected,film_status,34);expected[34]=0;film_status_update(NULL,&info);
  CHECK(!strcmp(val,expected)&&strlen(val)==34);}
 /* Inactive routing returns unchanged for every ordinary code and invalid payload. */
 for(i=-24;i<256;i++){e.type=0;e.param=i;e.obj=(void*)1;e.arg=14;CHECK(film_retro_event_gate(&e)==1);}
 CHECK(!stops&&!acks&&!av_reads);
 reset();open_ui();CHECK(key(BGMT_PRESS_DOWN)==0&&retro.selected==1);
 CHECK(key(BGMT_UNPRESS_DOWN)==0);CHECK(key(BGMT_WHEEL_DOWN)==0&&film_effect==1);
 CHECK(key(BGMT_Q)==0&&retro.page==1);CHECK(key(BGMT_PRESS_DOWN)==0);
 CHECK(key(BGMT_PRESS_SET)==0&&film_request==1);film_request=0;film_processing=1;
 CHECK(key(BGMT_PRESS_SET)==0&&!film_request);CHECK(key(BGMT_PRESS_DOWN)==0);
 film_auto=film_bench_request=1;
 CHECK(key(BGMT_PRESS_SET)==0&&film_cancel&&!film_auto&&!film_bench_request);
 for(i=0;i<6;i++){
  reset();open_ui();CHECK(key(presses[i])==0);CHECK(key(BGMT_MENU)==0&&!retro.visible);
  CHECK(key(releases[i])==0);CHECK(key(releases[i])==1);
  reset();open_ui();CHECK(key(presses[i])==0);movie=1;
  CHECK(key(0x7001)==2&&!retro.visible);CHECK(key(releases[i])==0);
 }
 for(i=0;i<(int)(sizeof native/sizeof native[0]);i++){
  reset();open_ui();e.type=0;e.param=native[i];e.obj=NULL;e.arg=0;
  CHECK(film_retro_event_gate(&e)==2&&!retro.visible);
  CHECK(film_retro_menu_passthrough(&e)==(native[i]<BGMT_PRESS_HALFSHUTTER));
  CHECK(!film_request&&!film_auto);
 }
 reset();open_ui();e.type=2;e.param=0x61;e.obj=(void*)1;e.arg=14;
 CHECK(film_retro_event_gate(&e)==1&&retro.visible&&!av_reads);
 e.type=0;e.arg=123;CHECK(film_retro_event_gate(&e)==1&&retro.visible&&!av_reads);
 shooting_mode=SHOOTMODE_M;e.arg=14;e.obj=NULL;
 CHECK(film_retro_event_gate(&e)==2&&!retro.visible&&!av_reads);
 e.obj=&i;av_status=0;CHECK(film_retro_event_gate(&e)==2&&av_reads==1&&!retro_av_handoff);
 CHECK(film_retro_event_gate(&e)==1&&av_reads==1);
 /* Queue an entire AV press/release before the native menu consumes either. */
 reset();open_ui();shooting_mode=SHOOTMODE_M;e.type=0;e.param=0x61;e.arg=14;e.obj=&i;
 av_status=1;CHECK(film_retro_event_gate(&e)==FILM_RETRO_NATIVE&&retro_av_handoff);
 CHECK(stops==1&&queued_toggles==1&&!retro.visible);
 av_status=0;CHECK(film_retro_event_gate(&e)==FILM_RETRO_NATIVE&&!retro_av_handoff);
 CHECK(stops==1&&queued_toggles==1&&av_reads==2);
 drain_menu_task();CHECK(!menu_on);
 reset();open_ui();entry_on=0;CHECK(key(BGMT_PRESS_RIGHT)==1&&!retro.visible&&film_effect==0&&!stops);
 reset();open_ui();menu_on=0;CHECK(key(BGMT_PRESS_DOWN)==1&&!retro.visible);
 for(i=0;i<2;i++){
  reset();open_ui();half=i;modeled_chain(BGMT_PRESS_HALFSHUTTER);
  CHECK(!retro.visible&&!stops&&native_half_signals==1&&redraw_flood_stopped&&queued_toggles==1);
  drain_menu_task();CHECK(!menu_on);
 }
 /* Exact r7 emulator ordering: a type-0 notification not defined by the
  * public T6 port arrives with half-press already active, before native72.
  * Keep76 a test observation, not an invented production button constant. */
 reset();open_ui();half=1;modeled_chain(76);
 CHECK(!retro.visible&&!stops&&!native_half_signals&&!queued_toggles);
 modeled_chain(BGMT_PRESS_HALFSHUTTER);
 CHECK(native_half_signals==1&&queued_toggles==1&&redraw_flood_stopped);
 drain_menu_task();CHECK(!menu_on);
 /* Capture precedence applies to other notifications and AV handoff too;
  * native half-press still reaches its normal menu handler exactly once. */
 reset();open_ui();half=1;modeled_chain(BGMT_ISO);
 CHECK(!retro.visible&&!stops&&!queued_toggles);
 modeled_chain(BGMT_PRESS_HALFSHUTTER);CHECK(queued_toggles==1);
 drain_menu_task();CHECK(!menu_on);
 reset();open_ui();half=1;shooting_mode=SHOOTMODE_M;
 e.type=0;e.param=GMT_OLC_INFO_CHANGED;e.arg=14;e.obj=&i;av_status=1;
 CHECK(film_retro_event_gate(&e)==FILM_RETRO_NATIVE&&!retro.visible&&!stops&&!queued_toggles);
 modeled_chain(BGMT_PRESS_HALFSHUTTER);CHECK(native_half_signals==1&&queued_toggles==1);
 drain_menu_task();CHECK(!menu_on);
 reset();open_ui();modeled_chain(BGMT_PRESS_FULLSHUTTER);
 CHECK(!retro.visible&&stops==1&&!native_half_signals&&queued_toggles==1);
 drain_menu_task();CHECK(!menu_on);
 reset();open_ui();half=1;end_poll=1;film_retro_poll(NULL);
 CHECK(!retro.visible&&!stops&&!queued_toggles);
 ml_shutdown_requested=0;modeled_chain(BGMT_PRESS_HALFSHUTTER);
 CHECK(native_half_signals==1&&queued_toggles==1);drain_menu_task();CHECK(!menu_on);
 reset();open_ui();lens_info.job_state=1;end_poll=1;film_retro_poll(NULL);
 CHECK(!retro.visible&&!stops&&!queued_toggles);
 ml_shutdown_requested=0;modeled_chain(BGMT_PRESS_HALFSHUTTER);
 CHECK(native_half_signals==1&&queued_toggles==1);drain_menu_task();CHECK(!menu_on);
 /* A busy OLC/property notification can likewise precede queued halfpress. */
 reset();open_ui();lens_info.job_state=1;e.type=0;e.param=GMT_OLC_INFO_CHANGED;e.obj=NULL;e.arg=123;
 CHECK(film_retro_event_gate(&e)==FILM_RETRO_PASS&&!retro.visible&&!stops&&!queued_toggles);
 modeled_chain(BGMT_PRESS_HALFSHUTTER);CHECK(native_half_signals==1&&queued_toggles==1);
 drain_menu_task();CHECK(!menu_on);
 reset();open_ui();film_menu[7].selected=1;
 film_retro_draw_menu(&film_menu[7],&info);CHECK(info.custom_drawing==CUSTOM_DRAW_THIS_MENU&&draws>25&&draws<100);
 retro.help=1;film_retro_draw_menu(&film_menu[7],&info);
 retro.help=0;retro.page=1;film_processing=1;film_retro_draw_menu(&film_menu[7],&info);
 puts("retro: inactive chain, native handoff, AV null/system payloads, press/release, mode/busy, values and bounded native renderer passed");
 return 0;
}
