/* Core failure/bounds tests; no camera or hardware I/O. GPL-2.0-only. */
#include "hevc_intra.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[32768];
    size_t used;
    unsigned reads,writes,fail_source,fail_sink,cancel_after,zero;
    hi_config *mutate;
} fake;
static int sink(void *opaque,const uint8_t *bytes,size_t n)
{
    fake *f=opaque;
    ++f->writes;
    if(f->fail_sink==f->writes)return 0;
    assert(n && n<=HI_OUTPUT_BYTES && n<=sizeof(f->bytes)-f->used);
    memcpy(f->bytes+f->used,bytes,n);f->used+=n;
    return 1;
}
static int source(void *opaque,uint32_t frame,uint32_t x,uint32_t y,uint8_t out[HI_CTU_BYTES])
{
    fake *f=opaque;
    unsigned i;
    assert(frame<2u && x<96u && y<96u && !(x&31u) && !(y&31u));
    ++f->reads;
    if(f->fail_source==f->reads)return 0;
    for(i=0;i<HI_CTU_BYTES;++i)out[i]=f->zero?0u:(uint8_t)(i+frame+x+y);
    if(f->mutate){f->mutate->width=2;f->mutate->height=2;f->mutate->frames=1;}
    return 1;
}
static int cancel(void *opaque)
{
    fake *f=opaque;
    return f->cancel_after && f->reads>=f->cancel_after-1u;
}
static hi_result run(hi_config *c,fake *f,hi_workspace *w,hi_stats *stats)
{
    hi_io io={f,sink,source,cancel};
    return hi_encode(c,&io,w,stats);
}
int main(void)
{
    hi_workspace workspace;
    hi_stats stats={123,456,789},before;
    hi_config config={64,64,2,32768};
    hi_io io;
    fake f;
    uint8_t default_output[32768];
    size_t default_size;
    unsigned i;
    memset(&f,0,sizeof(f));
    assert(run(&config,&f,&workspace,&stats)==HI_OK);
    assert(stats.bytes_committed==f.used && stats.frames_encoded==2 && stats.ctus_encoded==8);
    assert(f.reads==8 && f.writes>0);
    assert(f.used>64u*64u*3u);
    assert(!memcmp(f.bytes,"\0\0\0\1\x40\1",6));
    default_size=f.used;memcpy(default_output,f.bytes,default_size);
    memset(&f,0,sizeof(f));io.context=&f;io.write=sink;io.read_ctu=source;io.cancelled=cancel;
    assert(hi_encode_with_color(&config,&io,&workspace,&stats,HI_COLOR_UNSPECIFIED)==HI_OK);
    assert(f.used==default_size && !memcmp(f.bytes,default_output,default_size));
    memset(&f,0,sizeof(f));before=stats;
    assert(hi_encode_with_color(&config,&io,&workspace,&stats,(hi_color)2)==HI_BAD_ARGUMENT);
    assert(!f.reads && !f.writes && !memcmp(&stats,&before,sizeof(stats)));
    assert(hi_encode_with_color(&config,&io,&workspace,&stats,HI_COLOR_BT601_LIMITED_CENTERED)==HI_OK);
    assert(stats.frames_encoded==2 && f.reads==8);
    memset(&f,0,sizeof(f));f.mutate=&config;
    assert(run(&config,&f,&workspace,&stats)==HI_OK);
    assert(f.reads==8 && stats.frames_encoded==2 && config.width==2);
    config.width=64;config.height=64;config.frames=2;

    for(i=0;i<8u;++i){
        hi_config bad=config;
        if(i==0)bad.width=0;
        if(i==1)bad.height=3;
        if(i==2)bad.width=8194;
        if(i==3)bad.frames=0;
        if(i==4)bad.frames=HI_MAX_FRAMES+1u;
        if(i==5)bad.output_limit=0;
        if(i==6){bad.width=8192;bad.height=8192;}
        if(i==7){bad.width=8192;bad.height=4354;}
        memset(&f,0,sizeof(f));stats.bytes_committed=123;before=stats;
        assert(run(&bad,&f,&workspace,&stats)==HI_BAD_ARGUMENT);
        assert(f.reads==0 && f.writes==0 && !memcmp(&stats,&before,sizeof(stats)));
    }
    io.context=&f;io.write=sink;io.read_ctu=source;io.cancelled=cancel;
    assert(hi_encode(NULL,&io,&workspace,&stats)==HI_BAD_ARGUMENT);
    assert(hi_encode(&config,NULL,&workspace,&stats)==HI_BAD_ARGUMENT);
    assert(hi_encode(&config,&io,NULL,&stats)==HI_BAD_ARGUMENT);
    assert(hi_encode(&config,&io,&workspace,NULL)==HI_BAD_ARGUMENT);
    io.write=NULL;assert(hi_encode(&config,&io,&workspace,&stats)==HI_BAD_ARGUMENT);io.write=sink;
    io.read_ctu=NULL;assert(hi_encode(&config,&io,&workspace,&stats)==HI_BAD_ARGUMENT);io.read_ctu=source;
    assert(hi_encode(&config,&io,&workspace,(hi_stats *)(void *)&workspace)==HI_BAD_ARGUMENT);
    assert(hi_encode(&config,&io,(hi_workspace *)(UINTPTR_MAX-10u),&stats)==HI_BAD_ARGUMENT);

    memset(&f,0,sizeof(f));config.output_limit=8;
    assert(run(&config,&f,&workspace,&stats)==HI_OUTPUT_LIMIT);
    assert(!f.writes && !f.reads && !stats.bytes_committed);
    config.output_limit=4096;
    memset(&f,0,sizeof(f));
    assert(run(&config,&f,&workspace,&stats)==HI_OUTPUT_LIMIT);
    assert(stats.bytes_committed==4096 && f.used==4096 && !stats.frames_encoded);
    config.output_limit=32768;
    memset(&f,0,sizeof(f));f.fail_source=1;
    assert(run(&config,&f,&workspace,&stats)==HI_SOURCE_FAILED);
    assert(f.reads==1 && !f.writes && !stats.frames_encoded && !stats.ctus_encoded);
    memset(&f,0,sizeof(f));f.fail_source=4;
    assert(run(&config,&f,&workspace,&stats)==HI_SOURCE_FAILED);
    assert(f.reads==4 && stats.ctus_encoded==3 && !stats.frames_encoded);
    memset(&f,0,sizeof(f));f.fail_sink=1;
    assert(run(&config,&f,&workspace,&stats)==HI_SINK_FAILED);
    assert(f.writes==1 && !stats.bytes_committed && !stats.frames_encoded);
    memset(&f,0,sizeof(f));f.fail_sink=2;
    assert(run(&config,&f,&workspace,&stats)==HI_SINK_FAILED);
    assert(f.writes==2 && stats.bytes_committed==4096);
    memset(&f,0,sizeof(f));f.cancel_after=1;
    assert(run(&config,&f,&workspace,&stats)==HI_CANCELLED);
    assert(!f.reads && !f.writes);
    memset(&f,0,sizeof(f));f.cancel_after=2;
    assert(run(&config,&f,&workspace,&stats)==HI_CANCELLED);
    assert(f.reads==1 && !stats.frames_encoded && stats.ctus_encoded==1);
    memset(&f,0,sizeof(f));f.zero=1;config.frames=1;
    assert(run(&config,&f,&workspace,&stats)==HI_OK);
    /* Zero PCM bytes must be escaped; decoder pixel tests independently
     * establish that these inserted bytes do not alter samples. */
    assert(f.used>64u*64u*3u/2u+1024u);
    puts("HEVC I_PCM core: bounds, aliases, failures, limits, cancel, snapshots and escaping pass");
    return 0;
}
