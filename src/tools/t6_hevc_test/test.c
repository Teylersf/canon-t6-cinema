/* Actual native HEVC job + actual codec, mocked Canon I/O. GPL-2.0-only. */
#include "dryos.h"
#include "fio-ml.h"
#include "lens.h"
#include "propvalues.h"
#include "film_t6.h"
#include "film_hevc.h"
#include "hevc_intra.h"
#include <assert.h>

#define TEST_FILES 40u
#define TEST_CAPACITY (512u*1024u)
typedef struct { char path[32]; uint8_t *data; size_t length,capacity; } entry;
typedef struct { entry *file; size_t position; int writable; } handle;
enum guard { G_NONE,G_CANCEL,G_SHUTDOWN,G_CAPTURE,G_HALF,G_LV,G_MOVIE,G_RECORDING,G_DEADLINE };
static entry files[TEST_FILES];
static void *workspace_pointer,*verify_pointer,*report_pointer;
static unsigned handles,workspace_allocs,verify_allocs,removes,renames,reads,writes;
static unsigned guards,inject_guard,fail_workspace,fail_verify,fail_open_read;
static unsigned short_write,negative_write,short_read,zero_read,negative_read,over_read;
static unsigned corrupt_pass,size_failure,size_mismatch,rename_failure,race_tmp,race_dest;
static unsigned truncate_pass,hvc_read_done;
static enum guard final_size_interrupt;
static enum guard report_guard_kind;
static enum guard report_final_size_interrupt;
static struct film_hevc_results proof_snapshot;
static int have_proof_snapshot;
static unsigned report_guards,inject_report_guard,fail_report_memory;
static unsigned report_allocs,clear_flags_on_dma_free;
static unsigned report_fail_write,report_fail_read,report_fail_rename,report_corrupt_pass;
static unsigned report_race_tmp,report_race_dest,report_final_read_done;
static unsigned proof_write_sizes[16],proof_write_size_count,report_removes;
static unsigned short_write_call,negative_write_call,cancel_when_queued;
static unsigned bad_dma_class,bad_report_class,cacheable_calls;
static uint32_t removed_proof_crc;
static size_t removed_proof_bytes;
static uint32_t independent_crc(const uint8_t *,size_t);
static unsigned first_write_guard,first_tmp_read_guard,first_hvc_read_guard;
static enum guard guard_kind;
static uint32_t clock_ms;
static int stopped,half_pressed,movie;
static char status_text[64];
int ml_shutdown_requested,lv,recording;
struct mock_lens_info lens_info;
static const uint8_t foreign[]="KEEP-ME";

static entry *find(const char *path)
{
    unsigned i;
    for(i=0;i<TEST_FILES;++i)if(files[i].path[0] && !strcmp(files[i].path,path))return &files[i];
    return NULL;
}
static entry *create(const char *path,size_t capacity)
{
    unsigned i;
    assert(strlen(path)<32u && !find(path));
    for(i=0;i<TEST_FILES;++i)if(!files[i].path[0]){
        strcpy(files[i].path,path);files[i].data=malloc(capacity);assert(files[i].data);
        files[i].length=0;files[i].capacity=capacity;return &files[i];
    }
    assert(0);return NULL;
}
static void keep(const char *path)
{
    entry *file=create(path,sizeof(foreign));
    memcpy(file->data,foreign,sizeof(foreign));file->length=sizeof(foreign);
}
static void preserved(const char *path)
{
    entry *file=find(path);
    assert(file && file->length==sizeof(foreign) && !memcmp(file->data,foreign,sizeof(foreign)));
}
static unsigned ends(const char *path,const char *extension)
{
    size_t n=strlen(path),m=strlen(extension);
    return n>=m && !strcmp(path+n-m,extension);
}
static unsigned count_extension(const char *extension)
{
    unsigned i,n=0;
    for(i=0;i<TEST_FILES;++i)if(files[i].path[0] && ends(files[i].path,extension))++n;
    return n;
}
static void reset(void)
{
    unsigned i;
    assert(!handles && !workspace_pointer && !verify_pointer && !report_pointer);
    for(i=0;i<TEST_FILES;++i){free(files[i].data);memset(&files[i],0,sizeof(files[i]));}
    workspace_allocs=verify_allocs=removes=renames=reads=writes=guards=inject_guard=0;
    fail_workspace=fail_verify=fail_open_read=short_write=negative_write=short_read=zero_read=negative_read=over_read=0;
    corrupt_pass=size_failure=size_mismatch=rename_failure=race_tmp=race_dest=0;
    truncate_pass=hvc_read_done=0;final_size_interrupt=G_NONE;
    report_guards=inject_report_guard=fail_report_memory=0;report_guard_kind=G_NONE;
    report_allocs=clear_flags_on_dma_free=0;
    report_final_size_interrupt=G_NONE;have_proof_snapshot=0;memset(&proof_snapshot,0,sizeof(proof_snapshot));
    report_fail_write=report_fail_read=report_fail_rename=report_corrupt_pass=0;
    report_race_tmp=report_race_dest=report_final_read_done=report_removes=0;
    proof_write_size_count=short_write_call=negative_write_call=cancel_when_queued=0;
    bad_dma_class=bad_report_class=cacheable_calls=0;removed_proof_crc=0;removed_proof_bytes=0;
    memset(proof_write_sizes,0,sizeof(proof_write_sizes));
    first_write_guard=first_tmp_read_guard=first_hvc_read_guard=0;
    stopped=half_pressed=movie=ml_shutdown_requested=lv=recording=lens_info.job_state=0;
    guard_kind=G_NONE;clock_ms=0;status_text[0]=0;
}
void *test_malloc(size_t bytes)
{
    assert(bytes==sizeof(hi_workspace) && !workspace_pointer && !verify_pointer && !report_pointer);
    ++workspace_allocs;
    if(fail_workspace)return NULL;
    workspace_pointer=malloc(bytes);assert(workspace_pointer);return workspace_pointer;
}
void test_free(void *pointer)
{
    assert(pointer && pointer==workspace_pointer && !report_pointer);
    free(pointer);workspace_pointer=NULL;
}
void *fio_malloc(size_t bytes)
{
    if(bytes==FILM_HEVC_DMA_BYTES){
        assert(!verify_pointer && !report_pointer);
        ++verify_allocs;
        if(fail_verify)return NULL;
        verify_pointer=malloc(bytes);assert(verify_pointer);return verify_pointer;
    }
    assert(bytes==FILM_HEVC_REPORT_VERIFY_BYTES && !workspace_pointer && !verify_pointer && !report_pointer);
    ++report_allocs;
    if(fail_report_memory)return NULL;
    report_pointer=malloc(bytes);assert(report_pointer);return report_pointer;
}
void fio_free(void *pointer)
{
    assert(pointer);
    if(pointer==verify_pointer){
        free(pointer);verify_pointer=NULL;
        if(clear_flags_on_dma_free){stopped=half_pressed=movie=ml_shutdown_requested=lv=recording=lens_info.job_state=0;}
        return;
    }
    assert(pointer==report_pointer && !workspace_pointer && !verify_pointer);
    free(pointer);report_pointer=NULL;
}
void *test_cacheable(void *pointer)
{
    ++cacheable_calls;
    if(pointer==verify_pointer)return bad_dma_class?pointer:(void *)UINTPTR_MAX;
    if(pointer==report_pointer)return bad_report_class?pointer:(void *)UINTPTR_MAX;
    assert(pointer==workspace_pointer);
    return pointer;
}
unsigned get_ms_clock(void){return clock_ms++;}
void msleep(int milliseconds){assert(milliseconds>=0);clock_ms+=(uint32_t)milliseconds;}
static void apply_guard(enum guard kind)
{
    if(kind==G_CANCEL)stopped=1;
    else if(kind==G_SHUTDOWN)ml_shutdown_requested=1;
    else if(kind==G_CAPTURE)lens_info.job_state=1;
    else if(kind==G_HALF)half_pressed=1;
    else if(kind==G_LV)lv=1;
    else if(kind==G_MOVIE)movie=1;
    else if(kind==G_RECORDING)recording=1;
    else if(kind==G_DEADLINE){
        const struct film_hevc_results *r=film_hevc_get_results();
        clock_ms+=r->report_state==FILM_HEVC_REPORT_RUNNING?FILM_HEVC_REPORT_DEADLINE_MS:
            r->budget_ms?r->budget_ms:FILM_HEVC_DEADLINE_MS;
    }
}
int film_t6_cancelled(void)
{
    if(film_hevc_get_results()->report_state==FILM_HEVC_REPORT_RUNNING){
        if(!have_proof_snapshot){proof_snapshot=*film_hevc_get_results();have_proof_snapshot=1;}
        ++report_guards;
        if(inject_report_guard==report_guards)apply_guard(report_guard_kind);
        return stopped;
    }
    ++guards;
    if(inject_guard==guards){
        apply_guard(guard_kind);
    }
    if(cancel_when_queued && film_hevc_get_results()->write_callback_calls>=cancel_when_queued && !writes)stopped=1;
    return stopped;
}
int get_halfshutter_pressed(void){return half_pressed;}
int is_movie_mode(void){return movie;}
void film_t6_set_status(const char *text){assert(strlen(text)<sizeof(status_text));strcpy(status_text,text);}
int is_file(const char *path){return find(path)!=NULL;}
FILE *FIO_OpenFile(const char *path,unsigned mode)
{
    entry *file;
    handle *h;
    assert(strlen(path)==20u && !strncmp(path,"ML/FILM/HVC",11) && (path[11]=='T'||path[11]=='R'));
    file=find(path);
    if(mode==0x8501u){
        assert(ends(path,".TMP"));
        if((path[11]=='T'?race_tmp:report_race_tmp) && !file){keep(path);file=find(path);}
        if(file)return NULL;
        file=create(path,TEST_CAPACITY);
    }else{
        assert(mode==O_RDONLY);
        if(path[11]=='T')assert(!workspace_pointer && verify_pointer && !report_pointer);
        else assert(!workspace_pointer && !verify_pointer && report_pointer);
        if(!file || fail_open_read)return NULL;
    }
    h=malloc(sizeof(*h));assert(h);h->file=file;h->position=0;h->writable=mode==0x8501u;++handles;
    return (FILE *)(void *)h;
}
void FIO_CloseFile(FILE *stream){assert(stream && handles);--handles;free((handle *)(void *)stream);}
int FIO_WriteFile(FILE *stream,const void *data,size_t bytes)
{
    handle *h=(handle *)(void *)stream;
    size_t n=bytes;
    assert(h && h->writable && bytes);
    if(h->file->path[11]=='R'){
        assert(!workspace_pointer && !verify_pointer && bytes<FILM_HEVC_REPORT_MAX_BYTES);
        if(report_fail_write==1u)return -1;
        if(report_fail_write==2u && n)--n;
        assert(n<=h->file->capacity-h->position);
        memcpy(h->file->data+h->position,data,n);h->position+=n;h->file->length=h->position;
        return (int)n;
    }
    assert(verify_pointer==data && !report_pointer && bytes<=FILM_HEVC_DMA_BYTES);
    ++writes;if(!first_write_guard)first_write_guard=guards;
    if(proof_write_size_count<16u)proof_write_sizes[proof_write_size_count++]=(unsigned)bytes;
    if(negative_write || negative_write_call==writes)return -1;
    if((short_write || short_write_call==writes) && n)n--;
    assert(n<=h->file->capacity-h->position);
    memcpy(h->file->data+h->position,data,n);h->position+=n;h->file->length=h->position;
    return (int)n;
}
int FIO_ReadFile(FILE *stream,void *data,size_t bytes)
{
    handle *h=(handle *)(void *)stream;
    size_t n;
    unsigned pass=ends(h->file->path,".TMP")?1u:2u;
    assert(h && !h->writable && !workspace_pointer);
    if(h->file->path[11]=='R'){
        assert(!verify_pointer && report_pointer==data && bytes<=FILM_HEVC_REPORT_VERIFY_BYTES);
        if(report_fail_read)return 0;
        n=h->file->length-h->position;if(n>bytes)n=bytes;
        memcpy(data,h->file->data+h->position,n);h->position+=n;
        if(n && report_corrupt_pass==pass)((uint8_t *)data)[0]^=1u;
        if(pass==2u && h->position==h->file->length)report_final_read_done=1;
        return (int)n;
    }
    assert(verify_pointer==data && !report_pointer && bytes<=FILM_HEVC_DMA_BYTES);
    ++reads;
    if(pass==1u && !first_tmp_read_guard)first_tmp_read_guard=guards;
    if(pass==2u && !first_hvc_read_guard)first_hvc_read_guard=guards;
    if(truncate_pass==pass && !h->position){assert(h->file->length);--h->file->length;truncate_pass=0;}
    if(zero_read)return 0;
    if(negative_read)return -1;
    if(over_read)return (int)bytes+1;
    n=h->file->length-h->position;if(n>bytes)n=bytes;
    if(short_read && n>short_read)n=short_read;
    memcpy(data,h->file->data+h->position,n);h->position+=n;
    if(pass==2u && h->position==h->file->length)hvc_read_done=1;
    if(n && corrupt_pass==pass)((uint8_t *)data)[0]^=1u;
    return (int)n;
}
int FIO_GetFileSize(const char *path,uint32_t *size)
{
    entry *file=find(path);
    if(!file || (path[11]=='T' && size_failure))return -1;
    *size=(uint32_t)file->length;
    if(ends(path,".HVC") && hvc_read_done && final_size_interrupt!=G_NONE){apply_guard(final_size_interrupt);final_size_interrupt=G_NONE;}
    if(path[11]=='R' && ends(path,".CSV") && report_final_read_done && report_final_size_interrupt!=G_NONE){
        apply_guard(report_final_size_interrupt);report_final_size_interrupt=G_NONE;
    }
    if(path[11]=='T' && size_mismatch && *size)--*size;
    return 0;
}
int FIO_RenameFile(const char *source,const char *destination)
{
    entry *file=find(source);
    assert(!handles && !workspace_pointer && file && ends(source,".TMP"));
    if(source[11]=='R'){
        if(report_race_dest && !find(destination))keep(destination);
        if(report_fail_rename || find(destination))return -1;
        strcpy(file->path,destination);return 0;
    }
    ++renames;
    if(race_dest && !find(destination))keep(destination);
    if(rename_failure || find(destination))return -1;
    strcpy(file->path,destination);return 0;
}
int FIO_RemoveFile(const char *path)
{
    entry *file=find(path);
    assert(file && ends(path,".TMP") && file->capacity==TEST_CAPACITY && !handles);
    if(path[11]=='R')++report_removes;
    else{++removes;removed_proof_bytes=file->length;removed_proof_crc=independent_crc(file->data,file->length);}
    free(file->data);memset(file,0,sizeof(*file));return 0;
}
static uint32_t independent_crc(const uint8_t *bytes,size_t n)
{
    uint32_t crc=UINT32_MAX;
    size_t i;unsigned bit;
    for(i=0;i<n;++i){crc^=bytes[i];for(bit=0;bit<8u;++bit)crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);}
    return crc^UINT32_MAX;
}
static void settled(void)
{
    unsigned i;
    assert(!workspace_pointer && !verify_pointer && !report_pointer && !handles);
    for(i=0;i<TEST_FILES;++i)if(files[i].path[0] && ends(files[i].path,".TMP")){
        assert(files[i].capacity==sizeof(foreign));preserved(files[i].path);
    }
    if(have_proof_snapshot)
        assert(!memcmp(&proof_snapshot,film_hevc_get_results(),offsetof(struct film_hevc_results,report_state)));
}
static uint32_t csv_number(const entry *file,const char *key)
{
    char body[FILM_HEVC_REPORT_MAX_BYTES+1u],pattern[64],*where,*end;
    unsigned long value;
    assert(file && file->length<FILM_HEVC_REPORT_MAX_BYTES);
    memcpy(body,file->data,file->length);body[file->length]=0;
    assert(snprintf(pattern,sizeof(pattern),"%s,",key)>0);
    where=strstr(body,pattern);assert(where && (where==body || where[-1]=='\n'));
    value=strtoul(where+strlen(pattern),&end,10);
    assert(end>where+strlen(pattern) && *end=='\n' && value<=UINT32_MAX);
    return (uint32_t)value;
}
static void valid_report(void)
{
    const struct film_hevc_results *r=film_hevc_get_results();
    entry *file=find(r->report_path);
    unsigned i;
    assert(r->report_state==FILM_HEVC_REPORT_SAVED && file && r->report_bytes==file->length);
    assert(csv_number(file,"version")==2 && csv_number(file,"proof_state")==r->state);
    assert(csv_number(file,"requested_size")==r->size_choice && csv_number(file,"budget_ms")==r->budget_ms);
    assert(csv_number(file,"width")==r->width && csv_number(file,"height")==r->height);
    assert(csv_number(file,"frames_requested")==r->frames_requested && csv_number(file,"frames_encoded")==r->frames_encoded);
    assert(csv_number(file,"ctus_encoded")==r->ctus_encoded && csv_number(file,"codec_called")==r->codec_called);
    assert(csv_number(file,"codec_result")==r->codec_result && csv_number(file,"codec_bytes_committed")==r->codec_bytes_committed);
    assert(csv_number(file,"accepted_bytes")==r->accepted_bytes && csv_number(file,"written_bytes")==r->written_bytes);
    assert(csv_number(file,"crc32")==r->crc32 && csv_number(file,"proof_elapsed_ms")==r->elapsed_ms);
    assert(csv_number(file,"buffer_bytes")==FILM_HEVC_DMA_BYTES && csv_number(file,"workspace_bytes")==sizeof(hi_workspace));
    assert(csv_number(file,"write_callback_calls")==r->write_callback_calls && csv_number(file,"fio_write_calls")==r->fio_write_calls);
    assert(csv_number(file,"fio_read_calls")==r->fio_read_calls && csv_number(file,"fio_read_bytes")==r->fio_read_bytes);
    assert(csv_number(file,"phase_started_mask")==r->phase_started_mask && csv_number(file,"phase_completed_mask")==r->phase_completed_mask);
    assert(csv_number(file,"stopped_phase")==r->stopped_phase);
    for(i=0;i<FILM_HEVC_PHASE_COUNT;++i){
        const char *keys[]={"setup_ms","encode_ms","tmp_verify_ms","rename_ms","final_verify_ms"};
        assert(csv_number(file,keys[i])==r->phase_ms[i]);
    }
    assert(csv_number(file,"hvc_published")==!!r->path[0] && csv_number(file,"hvc_verified")==!!(r->state==FILM_HEVC_SAVED));
    assert(csv_number(file,"report_budget_ms")==FILM_HEVC_REPORT_DEADLINE_MS);
    assert(!csv_number(file,"camera_capture") && !csv_number(file,"recording_fps_measured"));
}
typedef struct { uint8_t data[TEST_CAPACITY]; size_t bytes; } canonical_stream;
static int canonical_write(void *opaque,const uint8_t *bytes,size_t n)
{
    canonical_stream *stream=opaque;
    assert(n<=sizeof(stream->data)-stream->bytes);
    memcpy(stream->data+stream->bytes,bytes,n);stream->bytes+=n;return 1;
}
static int canonical_source(void *opaque,uint32_t frame,uint32_t x,uint32_t y,uint8_t block[HI_CTU_BYTES])
{
    uint32_t row,column;
    (void)opaque;
    for(row=0;row<32u;++row)for(column=0;column<32u;++column)
        block[row*32u+column]=(uint8_t)(3u*(x+column)+5u*(y+row)+29u*frame);
    for(row=0;row<16u;++row)for(column=0;column<16u;++column){
        block[1024u+row*16u+column]=(uint8_t)(64u+7u*(x/2u+column)+3u*(y/2u+row)+11u*frame);
        block[1280u+row*16u+column]=(uint8_t)(192u+2u*(x/2u+column)+9u*(y/2u+row)+13u*frame);
    }
    return 1;
}
static void exact_canonical(const entry *file)
{
    canonical_stream *stream=calloc(1,sizeof(*stream));
    hi_workspace *workspace=malloc(sizeof(*workspace));
    hi_config config={320,180,2,TEST_CAPACITY};
    hi_io io={stream,canonical_write,canonical_source,NULL};
    hi_stats stats;
    assert(stream && workspace && hi_encode(&config,&io,workspace,&stats)==HI_OK);
    assert(file->length==stream->bytes && !memcmp(file->data,stream->data,stream->bytes));
    free(workspace);free(stream);
}
int main(int argc,char **argv)
{
    const struct film_hevc_results *result;
    unsigned baseline_guards,baseline_report_guards,write_guard,tmp_guard,hvc_guard,i,kind;
    entry *file;
    assert(independent_crc((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    reset();
    assert(film_hevc_generate(0)==0);result=film_hevc_get_results();settled();
    assert(result->state==FILM_HEVC_SAVED && result->width==320 && result->height==180);
    assert(result->frames_encoded==2 && result->ctus_encoded==120 && result->codec_result==HI_OK);
    assert(result->path[0] && workspace_allocs==1 && verify_allocs==1 && renames==1 && !removes);
    assert(result->version==2 && result->size_choice==0 && result->budget_ms==FILM_HEVC_DEADLINE_MS);
    assert(result->buffer_bytes==FILM_HEVC_DMA_BYTES && result->optimization_profile==FILM_HEVC_OPTIMIZATION_PROFILE);
    assert(result->codec_called==1 && result->accepted_bytes==result->bytes && result->written_bytes==result->bytes && result->codec_bytes_committed==result->bytes);
    assert(result->fio_write_calls==3 && writes==3 && proof_write_size_count==3 && proof_write_sizes[0]==65536 && proof_write_sizes[1]==65536 && proof_write_sizes[2]==result->bytes-131072u);
    assert(result->fio_read_calls==6 && result->fio_read_bytes==2u*result->bytes);
    assert(result->phase_started_mask==31 && result->phase_completed_mask==31 && result->stopped_phase==FILM_HEVC_PHASE_NONE);
    valid_report();
    file=find(result->path);assert(file && file->length==result->bytes);
    assert(independent_crc(file->data,file->length)==result->crc32);
    exact_canonical(file);
    if(argc>=2){FILE *out=fopen(argv[1],"wbx");assert(out);assert(fwrite(file->data,1,file->length,out)==file->length);assert(!fclose(out));}
    if(argc>=3){entry *csv=find(result->report_path);FILE *out=fopen(argv[2],"wbx");assert(out && csv);assert(fwrite(csv->data,1,csv->length,out)==csv->length);assert(!fclose(out));}
    baseline_guards=guards;write_guard=first_write_guard;tmp_guard=first_tmp_read_guard;hvc_guard=first_hvc_read_guard;
    baseline_report_guards=report_guards;
    assert(baseline_report_guards>0);
    assert(write_guard && tmp_guard>write_guard && hvc_guard>tmp_guard && baseline_guards>=hvc_guard);
    reset();clock_ms=UINT32_MAX-31u;
    assert(film_hevc_generate(0)==0);settled();assert(film_hevc_get_results()->elapsed_ms<FILM_HEVC_DEADLINE_MS);
    reset();assert(film_hevc_generate(3)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_BAD_SIZE && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED && !workspace_allocs);
    reset();fail_workspace=1;assert(film_hevc_generate(0)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_NO_MEMORY && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED && !verify_allocs);
    valid_report();
    reset();fail_verify=1;assert(film_hevc_generate(0)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_NO_MEMORY && !removes && !count_extension(".HVC") && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED);
    valid_report();
    reset();bad_dma_class=1;assert(film_hevc_generate(0)==-1);settled();result=film_hevc_get_results();
    assert(result->state==FILM_HEVC_MEMORY_CLASS_ERROR && !result->codec_called && !writes && !reads && !count_extension(".HVC"));valid_report();
    for(i=1;i<=2u;++i){reset();inject_guard=1;guard_kind=G_CANCEL;assert(film_hevc_generate(i)==1);settled();
        assert(film_hevc_get_results()->width==(i==1u?3840u:7680u) && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED);
        assert(film_hevc_get_results()->budget_ms==(i==1u?FILM_HEVC_4K_DEADLINE_MS:FILM_HEVC_8K_DEADLINE_MS));}
    for(i=0;i<3u;++i){reset();inject_guard=1;guard_kind=G_DEADLINE;assert(film_hevc_generate(i)==1);settled();result=film_hevc_get_results();
        assert(result->state==FILM_HEVC_DEADLINE && result->elapsed_ms>=result->budget_ms && !result->codec_called);
        assert(result->report_elapsed_ms<FILM_HEVC_REPORT_DEADLINE_MS);valid_report();}
    for(kind=G_CANCEL;kind<=G_DEADLINE;++kind){
        const unsigned points[]={1u,write_guard,tmp_guard,hvc_guard,baseline_guards};
        for(i=0;i<sizeof(points)/sizeof(points[0]);++i){
            reset();inject_guard=points[i];guard_kind=(enum guard)kind;
            assert(film_hevc_generate(0)==1);settled();result=film_hevc_get_results();
            assert(result->state==(kind==G_CANCEL||kind==G_SHUTDOWN?FILM_HEVC_CANCELLED:kind==G_DEADLINE?FILM_HEVC_DEADLINE:FILM_HEVC_BUSY));
            if(result->path[0])assert(find(result->path) && strstr(status_text,"kept"));
            else assert(!count_extension(".HVC"));
            if(points[i]==1u)assert(result->codec_result==FILM_HEVC_CODEC_NOT_CALLED);
        }
    }
    /* Exercise every actual cancellation and deadline guard, including all
     * CTU/write/readback/publication boundaries, with the real codec. */
    for(kind=G_CANCEL;kind<=G_DEADLINE;kind+=G_DEADLINE-G_CANCEL){
        for(i=1;i<=baseline_guards;++i){
            reset();inject_guard=i;guard_kind=(enum guard)kind;
            assert(film_hevc_generate(0)==1);settled();result=film_hevc_get_results();
            assert(result->state==(kind==G_CANCEL?FILM_HEVC_CANCELLED:FILM_HEVC_DEADLINE));
            if(result->path[0])assert(find(result->path));else assert(!count_extension(".HVC"));
        }
    }
    reset();keep("ML/FILM/HVCT0001.HVC");assert(film_hevc_generate(0)==0);settled();preserved("ML/FILM/HVCT0001.HVC");
    assert(!strcmp(film_hevc_get_results()->path,"ML/FILM/HVCT0002.HVC"));
    reset();keep("ML/FILM/HVCT0001.TMP");assert(film_hevc_generate(0)==0);preserved("ML/FILM/HVCT0001.TMP");assert(!removes);
    reset();for(i=1;i<=16u;++i){char path[32];snprintf(path,sizeof(path),"ML/FILM/HVCT%04u.HVC",i);keep(path);}
    assert(film_hevc_generate(0)==-1);settled();assert(count_extension(".HVC")==16 && !removes && !writes);
    reset();race_tmp=1;assert(film_hevc_generate(0)==-1);preserved("ML/FILM/HVCT0001.TMP");assert(!removes && !writes);
    reset();race_dest=1;assert(film_hevc_generate(0)==-1);settled();preserved("ML/FILM/HVCT0001.HVC");assert(removes==1);
    for(i=0;i<10u;++i){
        reset();
        if(i==0)short_write=1;
        if(i==1)negative_write=1;
        if(i==2)zero_read=1;
        if(i==3)negative_read=1;
        if(i==4)over_read=1;
        if(i==5)corrupt_pass=1;
        if(i==6)size_failure=1;
        if(i==7)size_mismatch=1;
        if(i==8)rename_failure=1;
        if(i==9)fail_open_read=1;
        assert(film_hevc_generate(0)==-1);settled();assert(!count_extension(".HVC") && removes==1);
    }
    reset();short_read=31;assert(film_hevc_generate(0)==0);settled();
    reset();corrupt_pass=2;assert(film_hevc_generate(0)==-1);settled();
    result=film_hevc_get_results();assert(result->state==FILM_HEVC_VERIFY_ERROR && result->path[0] && find(result->path) && !removes && strstr(status_text,"kept"));
    reset();truncate_pass=1;assert(film_hevc_generate(0)==-1);settled();assert(removes==1 && !count_extension(".HVC"));
    reset();truncate_pass=2;assert(film_hevc_generate(0)==-1);settled();
    assert(film_hevc_get_results()->path[0] && count_extension(".HVC")==1 && !removes);
    for(kind=G_CANCEL;kind<=G_DEADLINE;++kind){
        reset();final_size_interrupt=(enum guard)kind;
        assert(film_hevc_generate(0)==1);settled();result=film_hevc_get_results();
        assert(result->path[0] && find(result->path) && result->state!=FILM_HEVC_SAVED && !removes && strstr(status_text,"kept"));
    }
    /* A failed full aggregation drain can physically write a prefix longer
     * than the previously accepted whole codec callbacks. Never retry it. */
    reset();short_write_call=1;assert(film_hevc_generate(0)==-1);settled();result=film_hevc_get_results();
    assert(writes==1 && result->written_bytes==65535u && result->accepted_bytes==61440u &&
        result->codec_bytes_committed==61440u && result->written_bytes>result->accepted_bytes &&
        result->bytes==removed_proof_bytes && result->crc32==removed_proof_crc && !count_extension(".HVC"));valid_report();
    reset();short_write_call=3;assert(film_hevc_generate(0)==-1);settled();result=film_hevc_get_results();
    assert(writes==3 && result->codec_result==HI_OK && result->codec_bytes_committed==result->accepted_bytes &&
        result->written_bytes+1u==result->accepted_bytes && result->bytes==removed_proof_bytes &&
        result->crc32==removed_proof_crc && !count_extension(".HVC"));valid_report();
    reset();negative_write_call=3;assert(film_hevc_generate(0)==-1);settled();
    assert(writes==3 && film_hevc_get_results()->codec_result==HI_OK && film_hevc_get_results()->written_bytes==131072u);valid_report();
    reset();cancel_when_queued=3;assert(film_hevc_generate(0)==1);settled();result=film_hevc_get_results();
    assert(result->state==FILM_HEVC_CANCELLED && result->accepted_bytes>0 && !result->written_bytes && !writes &&
        !count_extension(".HVC") && result->report_state==FILM_HEVC_REPORT_SKIPPED_GUARD);
    for(kind=G_CANCEL;kind<=G_RECORDING;++kind){
        reset();inject_guard=write_guard;guard_kind=(enum guard)kind;clear_flags_on_dma_free=1;
        assert(film_hevc_generate(0)==1);settled();result=film_hevc_get_results();
        assert(result->state==(kind==G_CANCEL||kind==G_SHUTDOWN?FILM_HEVC_CANCELLED:FILM_HEVC_BUSY));
        assert(!stopped && !half_pressed && !movie && !ml_shutdown_requested && !lv && !recording && !lens_info.job_state);
        assert(result->report_state==FILM_HEVC_REPORT_SKIPPED_GUARD && !report_allocs && !count_extension(".CSV"));
    }
    reset();fail_report_memory=1;assert(film_hevc_generate(0)==0);settled();result=film_hevc_get_results();
    assert(result->state==FILM_HEVC_SAVED && result->path[0] && result->report_state==FILM_HEVC_REPORT_NO_MEMORY && !count_extension(".CSV"));
    reset();bad_report_class=1;assert(film_hevc_generate(0)==0);settled();result=film_hevc_get_results();
    assert(result->state==FILM_HEVC_SAVED && result->path[0] && result->report_state==FILM_HEVC_REPORT_MEMORY_CLASS_ERROR && !count_extension(".CSV"));
    for(i=0;i<6u;++i){
        reset();
        if(i==0)report_fail_write=1;
        if(i==1)report_fail_write=2;
        if(i==2)report_fail_read=1;
        if(i==3)report_fail_rename=1;
        if(i==4)report_corrupt_pass=1;
        if(i==5)report_corrupt_pass=2;
        assert(film_hevc_generate(0)==0);settled();result=film_hevc_get_results();
        assert(result->state==FILM_HEVC_SAVED && find(result->path) && result->report_state!=FILM_HEVC_REPORT_SAVED);
        if(i==5)assert(result->report_path[0] && find(result->report_path) && !report_removes);
        else assert(!count_extension(".CSV") && report_removes==1);
    }
    reset();keep("ML/FILM/HVCR0001.CSV");assert(film_hevc_generate(0)==0);settled();preserved("ML/FILM/HVCR0001.CSV");valid_report();
    assert(!strcmp(film_hevc_get_results()->report_path,"ML/FILM/HVCR0002.CSV"));
    reset();keep("ML/FILM/HVCR0001.TMP");assert(film_hevc_generate(0)==0);settled();preserved("ML/FILM/HVCR0001.TMP");valid_report();
    reset();for(i=1;i<=16u;++i){char path[32];snprintf(path,sizeof(path),"ML/FILM/HVCR%04u.CSV",i);keep(path);}
    assert(film_hevc_generate(0)==0);settled();assert(film_hevc_get_results()->state==FILM_HEVC_SAVED && film_hevc_get_results()->report_state==FILM_HEVC_REPORT_SLOTS_FULL && count_extension(".CSV")==16);
    reset();report_race_tmp=1;assert(film_hevc_generate(0)==0);settled();preserved("ML/FILM/HVCR0001.TMP");assert(!report_removes);
    reset();report_race_dest=1;assert(film_hevc_generate(0)==0);settled();preserved("ML/FILM/HVCR0001.CSV");assert(report_removes==1);
    for(kind=G_CANCEL;kind<=G_DEADLINE;kind+=G_DEADLINE-G_CANCEL){
        for(i=1;i<=baseline_report_guards;++i){
            reset();inject_report_guard=i;report_guard_kind=(enum guard)kind;
            assert(film_hevc_generate(0)==0);settled();result=film_hevc_get_results();
            assert(result->state==FILM_HEVC_SAVED && find(result->path) && result->report_state!=FILM_HEVC_REPORT_SAVED);
            if(result->report_path[0])assert(find(result->report_path));else assert(!count_extension(".CSV"));
        }
    }
    for(kind=G_CANCEL;kind<=G_DEADLINE;++kind){
        reset();report_final_size_interrupt=(enum guard)kind;
        assert(film_hevc_generate(0)==0);settled();result=film_hevc_get_results();
        assert(result->state==FILM_HEVC_SAVED && find(result->path) && result->report_path[0] &&
            find(result->report_path) && result->report_state!=FILM_HEVC_REPORT_SAVED && !report_removes);
    }
    reset();
    printf("Native HEVC: real codec/CRC, %u proof + %u report cancellation/deadline guards, 64KiB tails, publication, phases and I/O faults PASS\n",baseline_guards,baseline_report_guards);
    return 0;
}
