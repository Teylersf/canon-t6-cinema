/* Experimental HEVC I_PCM writer. GPL-2.0-only.
 * Syntax checked against HM16.18 and FFmpeg n6.1.1. Arithmetic interval
 * definitions/LPS constants are the normative HEVC CABAC tables, also exposed
 * by Kvazaar v2.3.2 (BSD-3-Clause). See README/THIRD_PARTY for provenance.
 */
#include "hevc_intra.h"

typedef struct {
    hi_config config;
    hi_io io;
    hi_workspace *workspace;
    hi_stats *stats;
    size_t used;
    uint32_t partial, partial_bits, zero_bytes;
    hi_color color;
    hi_result error;
} writer;

/* LPS range for the only used starting range class [448,510]. Our only
 * context-coded symbol is part_mode=2Nx2N, initially MPS1 at QP26/init184.
 * Every CU is32x32 and PCM; no split, prediction or coefficient bins exist.
 * The MPS state advances by1 to62; the arithmetic engine restarts after PCM.
 */
static const uint8_t lps_high[63] = {
    240,227,216,205,195,185,175,166,158,150,142,135,128,122,116,110,
    104,99,94,89,85,80,76,72,69,65,62,59,56,53,50,48,45,43,41,
    39,37,35,33,31,30,28,27,25,24,23,22,21,20,19,18,17,16,
    15,14,14,13,12,12,11,11,10,9
};

static int flush(writer *w)
{
    if (w->error != HI_OK) return 0;
    if (w->used && w->io.write(w->io.context,w->workspace->output,w->used)!=1) {
        w->error=HI_SINK_FAILED; return 0;
    }
    w->stats->bytes_committed+=w->used;
    w->used=0;
    return 1;
}
static void byte(writer *w,uint32_t value)
{
    if(w->error!=HI_OK) return;
    if(w->stats->bytes_committed+(uint64_t)w->used>=w->config.output_limit){
        w->error=HI_OUTPUT_LIMIT;return;
    }
    w->workspace->output[w->used++]=(uint8_t)value;
    if(w->used==HI_OUTPUT_BYTES) (void)flush(w);
}
static void rbsp_byte(writer *w,uint32_t value)
{
    if(w->zero_bytes==2u && value<=3u){byte(w,3);w->zero_bytes=0;}
    byte(w,value);
    w->zero_bytes=value==0u?w->zero_bytes+1u:0u;
}
static void bits(writer *w,uint32_t value,unsigned count)
{
    while(count){
        --count;
        w->partial=(w->partial<<1)|((value>>count)&1u);
        if(++w->partial_bits==8u){rbsp_byte(w,w->partial);w->partial=0;w->partial_bits=0;}
    }
}
static void ue(writer *w,uint32_t value)
{
    uint32_t code=value+1u,tmp=code;unsigned n=0;
    while(tmp>>=1)++n;
    bits(w,0,n);bits(w,code,n+1u);
}
static void align_one(writer *w)
{
    bits(w,1,1);
    while(w->partial_bits)bits(w,0,1);
}
static void nal(writer *w,unsigned type)
{
    byte(w,0);byte(w,0);byte(w,0);byte(w,1);
    byte(w,type<<1);byte(w,1);
    w->zero_bytes=0;w->partial=0;w->partial_bits=0;
}
static void ptl(writer *w)
{
    bits(w,0,2);bits(w,1,1);bits(w,1,5); /* Main, high tier */
    bits(w,UINT32_C(0x40000000),32); /* Main compatibility */
    bits(w,11,4); /* progressive, no interlaced, nonpacked, frame-only */
    bits(w,0,32);bits(w,0,12);bits(w,186,8); /* level6.2 */
}
static void color_vui(writer *w)
{
    bits(w,0,1); /* aspect_ratio_info_present */
    bits(w,0,1); /* overscan_info_present */
    bits(w,1,1); /* video_signal_type_present */
    bits(w,5,3); /* unspecified video format */
    bits(w,0,1); /* limited range */
    bits(w,1,1); /* colour_description_present */
    bits(w,2,8);bits(w,2,8);bits(w,6,8); /* unspecified primaries/transfer, BT.601 matrix */
    bits(w,1,1);ue(w,1);ue(w,1); /* centered chroma in both fields */
    bits(w,0,1);bits(w,0,1);bits(w,0,1); /* neutral chroma, field seq, frame info */
    bits(w,0,1); /* default display window */
    bits(w,0,1); /* timing_info_present: NO FPS assertion */
    bits(w,0,1); /* bitstream restriction */
}
static void parameter_sets(writer *w)
{
    uint32_t cw=(w->config.width+31u)&~31u;
    uint32_t ch=(w->config.height+31u)&~31u;
    nal(w,32);bits(w,0,4);bits(w,3,2);bits(w,0,6);bits(w,0,3);
    bits(w,1,1);bits(w,65535,16);ptl(w);bits(w,0,1);
    ue(w,0);ue(w,0);ue(w,0);bits(w,0,6);ue(w,0);bits(w,0,1);bits(w,0,1);align_one(w);
    nal(w,33);bits(w,0,4);bits(w,0,3);bits(w,1,1);ptl(w);
    ue(w,0);ue(w,1);ue(w,cw);ue(w,ch);
    bits(w,(cw!=w->config.width || ch!=w->config.height)?1u:0u,1);
    if(cw!=w->config.width || ch!=w->config.height){
        ue(w,0);ue(w,(cw-w->config.width)/2u);ue(w,0);ue(w,(ch-w->config.height)/2u);
    }
    ue(w,0);ue(w,0);ue(w,0);bits(w,0,1);ue(w,0);ue(w,0);ue(w,0);
    ue(w,2);ue(w,0);ue(w,0);ue(w,3);ue(w,0);ue(w,0);
    bits(w,0,1);bits(w,0,1);bits(w,0,1);bits(w,1,1);
    bits(w,7,4);bits(w,7,4);ue(w,2);ue(w,0);bits(w,1,1);
    ue(w,0);bits(w,0,1);bits(w,0,1);bits(w,0,1);
    bits(w,w->color==HI_COLOR_UNSPECIFIED?0u:1u,1);
    if(w->color!=HI_COLOR_UNSPECIFIED)color_vui(w);
    bits(w,0,1);align_one(w);
    nal(w,34);ue(w,0);ue(w,0);bits(w,0,1);bits(w,0,1);bits(w,0,3);
    bits(w,0,1);bits(w,0,1);ue(w,0);ue(w,0);ue(w,0);
    bits(w,0,1);bits(w,0,1);bits(w,0,1);ue(w,0);ue(w,0);
    bits(w,0,1);bits(w,0,1);bits(w,0,1);bits(w,0,1);bits(w,0,1);bits(w,0,1);
    bits(w,0,1);bits(w,1,1);bits(w,0,1);bits(w,1,1);
    bits(w,0,1);bits(w,0,1);ue(w,0);bits(w,0,1);bits(w,0,1);align_one(w);
}
static int canceled(writer *w)
{
    if(w->io.cancelled && w->io.cancelled(w->io.context)){
        w->error=HI_CANCELLED;return 1;
    }
    return 0;
}
static int span(uintptr_t at,size_t n,uintptr_t *end)
{
    if(n>UINTPTR_MAX-at)return 0;
    *end=at+n;
    return 1;
}
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b,ae,be;
    return !span(aa,an,&ae)||!span(bb,bn,&be)||(aa<be&&bb<ae);
}
static hi_result encode_impl(const hi_config *config,const hi_io *io,hi_workspace *workspace,hi_stats *stats,hi_color color)
{
    writer w;
    uint32_t frame,x,y,i,cw,ch,context;
    if(!config||!io||!workspace||!stats||!io->write||!io->read_ctu)return HI_BAD_ARGUMENT;
    if(color!=HI_COLOR_UNSPECIFIED && color!=HI_COLOR_BT601_LIMITED_CENTERED)return HI_BAD_ARGUMENT;
    if(config->width<2u||config->height<2u||config->width>HI_MAX_DIMENSION||
       config->height>HI_MAX_DIMENSION||(config->width&1u)||(config->height&1u)||
       !config->frames||config->frames>HI_MAX_FRAMES||!config->output_limit)return HI_BAD_ARGUMENT;
    cw=(config->width+31u)&~31u;ch=(config->height+31u)&~31u;
    if((uint64_t)cw*ch>HI_MAX_LUMA_PS)return HI_BAD_ARGUMENT;
    if(overlap(workspace,sizeof(*workspace),stats,sizeof(*stats))||
       overlap(workspace,sizeof(*workspace),config,sizeof(*config))||
       overlap(workspace,sizeof(*workspace),io,sizeof(*io))||
       overlap(stats,sizeof(*stats),config,sizeof(*config))||
       overlap(stats,sizeof(*stats),io,sizeof(*io)))return HI_BAD_ARGUMENT;
    w.config=*config;w.io=*io;w.workspace=workspace;w.stats=stats;w.color=color;
    w.used=0;w.partial=0;w.partial_bits=0;w.zero_bytes=0;w.error=HI_OK;
    stats->bytes_committed=0;stats->frames_encoded=0;stats->ctus_encoded=0;
    if(canceled(&w))return w.error;
    parameter_sets(&w);
    cw=(w.config.width+31u)&~31u;ch=(w.config.height+31u)&~31u;
    for(frame=0;frame<w.config.frames && w.error==HI_OK;++frame){
        if(canceled(&w))break;
        nal(&w,20);bits(&w,1,1);bits(&w,0,1);ue(&w,0);ue(&w,2);ue(&w,0);align_one(&w);
        context=0;
        for(y=0;y<ch && w.error==HI_OK;y+=32u){
            for(x=0;x<cw && w.error==HI_OK;x+=32u){
                uint32_t initial_range=(x==0u&&y==0u)?510u:508u;
                if(canceled(&w))break;
                if(w.io.read_ctu(w.io.context,frame,x,y,workspace->ctu)!=1){w.error=HI_SOURCE_FAILED;break;}
                /* part_mode MPS1 then PCM termination1. HM finish plus1 and
                 * zero alignment yield this two-byte arithmetic interval. */
                rbsp_byte(&w,(initial_range-lps_high[context]-2u)>>1);
                rbsp_byte(&w,128);
                if(context<62u)++context;
                for(i=0;i<HI_CTU_BYTES && w.error==HI_OK;++i)rbsp_byte(&w,workspace->ctu[i]);
                if(w.error==HI_OK)++stats->ctus_encoded;
            }
        }
        if(w.error==HI_OK){rbsp_byte(&w,254);rbsp_byte(&w,128);if(flush(&w))++stats->frames_encoded;}
    }
    return w.error;
}
hi_result hi_encode(const hi_config *config,const hi_io *io,hi_workspace *workspace,hi_stats *stats)
{
    return encode_impl(config,io,workspace,stats,HI_COLOR_UNSPECIFIED);
}
hi_result hi_encode_with_color(const hi_config *config,const hi_io *io,hi_workspace *workspace,hi_stats *stats,hi_color color)
{
    return encode_impl(config,io,workspace,stats,color);
}
