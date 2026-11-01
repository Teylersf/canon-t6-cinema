/* The actual portable renderer emits commands; host fonts/palette are a
 * layout reference, never a photograph or physical-camera screenshot. */
#include "film_retro_draw.h"
#include <stdio.h>
#include <stdlib.h>
static int count;
static void fill(void *ctx,int x,int y,int w,int h,int color){
 (void)ctx;printf("%s[\"fill\",%d,%d,%d,%d,%d]",count++?",":"",x,y,w,h,color);
}
static void text(void *ctx,int x,int y,int size,int color,const char *s){
 (void)ctx;printf("%s[\"text\",%d,%d,%d,%d,\"",count++?",":"",x,y,size,color);
 while(*s){if(*s=='"'||*s=='\\')putchar('\\');putchar(*s++);}printf("\"]");
}
int main(int argc,char **argv){
 struct film_retro_view v={0};struct film_retro_canvas c={NULL,fill,text};
 int page=argc>1?atoi(argv[1]):0;
 v.page=page==1;v.help=page==2;v.selected=page==1?2:1;
 v.busy=page==1;v.auto_on=page==1;v.effect=2;v.stock_count=13;v.stock=11;
 v.stock_name="SOUP 800";v.status=page==1?"Developing separate JPEG...":"Ready / source CR2 preserved";
 printf("{\"width\":720,\"height\":480,\"ops\":[");film_retro_draw(&v,&c);printf("]}\n");return 0;
}
