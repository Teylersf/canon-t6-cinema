/* Host-only streaming raw420 file adapter. GPL-2.0-only. No camera access. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "hevc_intra.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int input,output;
    uint32_t width,height,coded_width,loaded_frame,loaded_y;
    uint64_t frame_bytes;
    uint8_t *row;
} raw_source;

static int read_exact(int fd,uint8_t *out,size_t bytes,uint64_t offset)
{
    size_t done=0;
    while(done<bytes){
        ssize_t n=pread(fd,out+done,bytes-done,(off_t)(offset+done));
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        done+=(size_t)n;
    }
    return 1;
}
static int write_exact(void *opaque,const uint8_t *data,size_t bytes)
{
    raw_source *s=opaque;
    size_t done=0;
    while(done<bytes){
        ssize_t n=write(s->output,data+done,bytes-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        done+=(size_t)n;
    }
    return 1;
}
static int load_row(raw_source *s,uint32_t frame,uint32_t y)
{
    uint32_t plane,line;
    uint64_t frame_start=(uint64_t)frame*s->frame_bytes;
    size_t luma=(size_t)s->coded_width*32u;
    size_t chroma=(size_t)(s->coded_width/2u)*16u;
    memset(s->row,0,luma+2u*chroma);
    for(plane=0;plane<3u;++plane){
        uint32_t pw=plane?s->width/2u:s->width;
        uint32_t ph=plane?s->height/2u:s->height;
        uint32_t cy=plane?y/2u:y;
        uint32_t lines=plane?16u:32u;
        size_t stride=plane?s->coded_width/2u:s->coded_width;
        size_t base=plane?luma+(plane-1u)*chroma:0u;
        uint64_t plane_start=frame_start+(plane?(uint64_t)s->width*s->height:0u)+
            (plane==2u?(uint64_t)(s->width/2u)*(s->height/2u):0u);
        for(line=0;line<lines && cy+line<ph;++line){
            if(!read_exact(s->input,s->row+base+(size_t)line*stride,pw,
                plane_start+(uint64_t)(cy+line)*pw))return 0;
        }
    }
    s->loaded_frame=frame;s->loaded_y=y;
    return 1;
}
static int read_ctu(void *opaque,uint32_t frame,uint32_t x,uint32_t y,uint8_t out[HI_CTU_BYTES])
{
    raw_source *s=opaque;
    uint32_t plane,line;
    size_t luma=(size_t)s->coded_width*32u;
    size_t chroma=(size_t)(s->coded_width/2u)*16u;
    if(s->loaded_frame!=frame || s->loaded_y!=y){
        if(x || !load_row(s,frame,y))return 0;
    }
    for(plane=0;plane<3u;++plane){
        size_t stride=plane?s->coded_width/2u:s->coded_width;
        size_t start=plane?luma+(plane-1u)*chroma:0u;
        size_t dst=plane?1024u+(plane-1u)*256u:0u;
        uint32_t side=plane?16u:32u;
        uint32_t cx=plane?x/2u:x;
        for(line=0;line<side;++line)
            memcpy(out+dst+(size_t)line*side,s->row+start+(size_t)line*stride+cx,side);
    }
    return 1;
}
static int number(const char *text,uint32_t *value)
{
    char *end=NULL;unsigned long n;
    if(!text[0]||text[0]=='-')return 0;
    errno=0;n=strtoul(text,&end,10);
    if(errno||*end||n>UINT32_MAX)return 0;
    *value=(uint32_t)n;return 1;
}
int main(int argc,char **argv)
{
    hi_config config={0,0,0,UINT64_C(1600000000)};
    hi_workspace workspace;
    hi_stats stats;
    raw_source source={0};
    hi_io io={&source,write_exact,read_ctu,NULL};
    hi_result result;
    struct stat input_stat;
    uint32_t coded_height;
    int exit_code=2;
    hi_color color=HI_COLOR_UNSPECIFIED;
    if((argc!=6 && argc!=7)||!number(argv[1],&config.width)||!number(argv[2],&config.height)||!number(argv[3],&config.frames)||
       (argc==7 && strcmp(argv[6],"--bt601-limited-centered"))){
        fputs("usage: hevc-intra-raw420 WIDTH HEIGHT FRAMES INPUT.yuv NEW.hevc [--bt601-limited-centered]\n",stderr);return 2;
    }
    if(argc==7)color=HI_COLOR_BT601_LIMITED_CENTERED;
    if(config.width<2u||config.height<2u||config.width>HI_MAX_DIMENSION||config.height>HI_MAX_DIMENSION||
       (config.width&1u)||(config.height&1u)||!config.frames||config.frames>HI_MAX_FRAMES){
        fputs("dimensions/frame count outside encoder bounds\n",stderr);return 2;
    }
    source.width=config.width;source.height=config.height;
    source.coded_width=(config.width+31u)&~31u;
    coded_height=(config.height+31u)&~31u;
    if((uint64_t)source.coded_width*coded_height>HI_MAX_LUMA_PS){fputs("coded area exceeds level6.2\n",stderr);return 2;}
    source.frame_bytes=(uint64_t)source.width*source.height*3u/2u;
    source.loaded_frame=UINT32_MAX;source.loaded_y=UINT32_MAX;
    /* O_NONBLOCK lets fstat reject a FIFO/device without hanging on open. */
    source.input=open(argv[4],O_RDONLY|O_NONBLOCK);
    if(source.input<0){perror("input");return 2;}
    if(fstat(source.input,&input_stat)||!S_ISREG(input_stat.st_mode)||input_stat.st_size<0||
       (uint64_t)input_stat.st_size!=source.frame_bytes*config.frames){
        fputs("input must be a regular raw420 file of exactly the expected length\n",stderr);
        (void)close(source.input);return 2;
    }
    source.row=malloc((size_t)source.coded_width*48u);
    if(!source.row){fputs("CTU row allocation failed\n",stderr);(void)close(source.input);return 2;}
    source.output=open(argv[5],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(source.output<0){perror("exclusive output");free(source.row);(void)close(source.input);return 2;}
    result=hi_encode_with_color(&config,&io,&workspace,&stats,color);
    if(result!=HI_OK)fprintf(stderr,"encoder stopped: %d; output is partial\n",(int)result);
    else{
        printf("HEVC I_PCM raw420: %u x %u, %u frames, %llu bytes; CTU row %zu bytes\n",
            config.width,config.height,stats.frames_encoded,(unsigned long long)stats.bytes_committed,(size_t)source.coded_width*48u);
        exit_code=0;
    }
    if(close(source.output)){perror("output close");exit_code=2;}
    if(close(source.input)){perror("input close");exit_code=2;}
    free(source.row);
    return exit_code;
}
