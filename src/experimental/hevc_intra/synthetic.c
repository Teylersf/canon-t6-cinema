/* Host-only synthetic fixture CLI. GPL-2.0-only. No camera access. */
#define _POSIX_C_SOURCE 200809L
#include "hevc_intra.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { int fd; int zeros; } fixture;
static int put(void *opaque,const uint8_t *bytes,size_t count)
{
    fixture *f=opaque;
    size_t done=0;
    while(done<count){
        ssize_t n=write(f->fd,bytes+done,count-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        done+=(size_t)n;
    }
    return 1;
}
static int block(void *opaque,uint32_t frame,uint32_t x,uint32_t y,uint8_t out[HI_CTU_BYTES])
{
    uint32_t xx,yy;
    fixture *f=opaque;
    if(f->zeros){memset(out,0,HI_CTU_BYTES);return 1;}
    for(yy=0;yy<32u;++yy)for(xx=0;xx<32u;++xx)
        out[yy*32u+xx]=(uint8_t)((x+xx)*3u+(y+yy)*5u+frame*29u);
    for(yy=0;yy<16u;++yy)for(xx=0;xx<16u;++xx){
        out[1024u+yy*16u+xx]=(uint8_t)(64u+(x/2u+xx)*7u+(y/2u+yy)*3u+frame*11u);
        out[1280u+yy*16u+xx]=(uint8_t)(192u+(x/2u+xx)*2u+(y/2u+yy)*9u+frame*13u);
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
    fixture f;
    hi_io io={&f,put,block,NULL};
    hi_result result;
    if((argc!=5 && argc!=6)||!number(argv[1],&config.width)||!number(argv[2],&config.height)||!number(argv[3],&config.frames)||
       (argc==6 && strcmp(argv[5],"--zeros"))){
        fputs("usage: hevc-intra-synthetic WIDTH HEIGHT FRAMES NEW.hevc [--zeros]\n",stderr);return 2;
    }
    f.zeros=argc==6;
    f.fd=open(argv[4],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(f.fd<0){perror("exclusive output");return 2;}
    result=hi_encode(&config,&io,&workspace,&stats);
    if(close(f.fd)){perror("close");return 2;}
    if(result!=HI_OK){fprintf(stderr,"encoder stopped: %d; output is partial\n",(int)result);return 2;}
    printf("HEVC I_PCM: %u x %u, %u frames, %u CTUs, %llu bytes; synthetic only\n",
        config.width,config.height,stats.frames_encoded,stats.ctus_encoded,(unsigned long long)stats.bytes_committed);
    return 0;
}
