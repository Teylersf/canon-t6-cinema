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
static void *workspace_pointer,*verify_pointer;
static unsigned handles,workspace_allocs,verify_allocs,removes,renames,reads,writes;
static unsigned guards,inject_guard,fail_workspace,fail_verify,fail_open_read;
static unsigned short_write,negative_write,short_read,zero_read,negative_read,over_read;
static unsigned corrupt_pass,size_failure,size_mismatch,rename_failure,race_tmp,race_dest;
static unsigned truncate_pass,hvc_read_done;
static enum guard final_size_interrupt;
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
    assert(!handles && !workspace_pointer && !verify_pointer);
    for(i=0;i<TEST_FILES;++i){free(files[i].data);memset(&files[i],0,sizeof(files[i]));}
    workspace_allocs=verify_allocs=removes=renames=reads=writes=guards=inject_guard=0;
    fail_workspace=fail_verify=fail_open_read=short_write=negative_write=short_read=zero_read=negative_read=over_read=0;
    corrupt_pass=size_failure=size_mismatch=rename_failure=race_tmp=race_dest=0;
    truncate_pass=hvc_read_done=0;final_size_interrupt=G_NONE;
    first_write_guard=first_tmp_read_guard=first_hvc_read_guard=0;
    stopped=half_pressed=movie=ml_shutdown_requested=lv=recording=lens_info.job_state=0;
    guard_kind=G_NONE;clock_ms=0;status_text[0]=0;
}
void *test_malloc(size_t bytes)
{
    assert(bytes==sizeof(hi_workspace) && !workspace_pointer && !verify_pointer);
    ++workspace_allocs;
    if(fail_workspace)return NULL;
    workspace_pointer=malloc(bytes);assert(workspace_pointer);return workspace_pointer;
}
void test_free(void *pointer)
{
    assert(pointer && pointer==workspace_pointer && !verify_pointer);
    free(pointer);workspace_pointer=NULL;
}
void *fio_malloc(size_t bytes)
{
    assert(bytes==4096u && !workspace_pointer && !verify_pointer);
    ++verify_allocs;
    if(fail_verify)return NULL;
    verify_pointer=malloc(bytes);assert(verify_pointer);return verify_pointer;
}
void fio_free(void *pointer)
{
    assert(pointer && pointer==verify_pointer && !workspace_pointer);
    free(pointer);verify_pointer=NULL;
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
    else if(kind==G_DEADLINE)clock_ms+=FILM_HEVC_DEADLINE_MS;
}
int film_t6_cancelled(void)
{
    ++guards;
    if(inject_guard==guards){
        apply_guard(guard_kind);
    }
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
    assert(strlen(path)==20u && !strncmp(path,"ML/FILM/HVCT",12));
    file=find(path);
    if(mode==0x8501u){
        assert(ends(path,".TMP"));
        if(race_tmp && !file){keep(path);file=find(path);}
        if(file)return NULL;
        file=create(path,TEST_CAPACITY);
    }else{
        assert(mode==O_RDONLY);
        assert(!workspace_pointer && verify_pointer);
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
    assert(h && h->writable && workspace_pointer && !verify_pointer && bytes && bytes<=4096u);
    ++writes;if(!first_write_guard)first_write_guard=guards;
    if(negative_write)return -1;
    if(short_write && n)n--;
    assert(n<=h->file->capacity-h->position);
    memcpy(h->file->data+h->position,data,n);h->position+=n;h->file->length=h->position;
    return (int)n;
}
int FIO_ReadFile(FILE *stream,void *data,size_t bytes)
{
    handle *h=(handle *)(void *)stream;
    size_t n;
    unsigned pass=ends(h->file->path,".TMP")?1u:2u;
    assert(h && !h->writable && !workspace_pointer && verify_pointer==data && bytes<=4096u);
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
    if(size_failure || !file)return -1;
    *size=(uint32_t)file->length;
    if(ends(path,".HVC") && hvc_read_done && final_size_interrupt!=G_NONE){apply_guard(final_size_interrupt);final_size_interrupt=G_NONE;}
    if(size_mismatch && *size)--*size;
    return 0;
}
int FIO_RenameFile(const char *source,const char *destination)
{
    entry *file=find(source);
    ++renames;assert(!handles && !workspace_pointer && file && ends(source,".TMP"));
    if(race_dest && !find(destination))keep(destination);
    if(rename_failure || find(destination))return -1;
    strcpy(file->path,destination);return 0;
}
int FIO_RemoveFile(const char *path)
{
    entry *file=find(path);
    assert(file && ends(path,".TMP") && file->capacity==TEST_CAPACITY && !handles);
    ++removes;free(file->data);memset(file,0,sizeof(*file));return 0;
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
    assert(!workspace_pointer && !verify_pointer && !handles);
    assert(!count_extension(".TMP"));
}
int main(int argc,char **argv)
{
    const struct film_hevc_results *result;
    unsigned baseline_guards,write_guard,tmp_guard,hvc_guard,i,kind;
    entry *file;
    assert(independent_crc((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    reset();
    assert(film_hevc_generate(0)==0);result=film_hevc_get_results();settled();
    assert(result->state==FILM_HEVC_SAVED && result->width==320 && result->height==180);
    assert(result->frames_encoded==2 && result->ctus_encoded==120 && result->codec_result==HI_OK);
    assert(result->path[0] && workspace_allocs==1 && verify_allocs==1 && renames==1 && !removes);
    file=find(result->path);assert(file && file->length==result->bytes);
    assert(independent_crc(file->data,file->length)==result->crc32);
    if(argc==2){FILE *out=fopen(argv[1],"wbx");assert(out);assert(fwrite(file->data,1,file->length,out)==file->length);assert(!fclose(out));}
    baseline_guards=guards;write_guard=first_write_guard;tmp_guard=first_tmp_read_guard;hvc_guard=first_hvc_read_guard;
    assert(write_guard && tmp_guard>write_guard && hvc_guard>tmp_guard && baseline_guards>=hvc_guard);
    reset();clock_ms=UINT32_MAX-31u;
    assert(film_hevc_generate(0)==0);settled();assert(film_hevc_get_results()->elapsed_ms<FILM_HEVC_DEADLINE_MS);
    reset();assert(film_hevc_generate(3)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_BAD_SIZE && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED && !workspace_allocs);
    reset();fail_workspace=1;assert(film_hevc_generate(0)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_NO_MEMORY && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED && !verify_allocs);
    reset();fail_verify=1;assert(film_hevc_generate(0)==-1);settled();
    assert(film_hevc_get_results()->state==FILM_HEVC_NO_MEMORY && removes==1 && !count_extension(".HVC"));
    for(i=1;i<=2u;++i){reset();inject_guard=1;guard_kind=G_CANCEL;assert(film_hevc_generate(i)==1);settled();
        assert(film_hevc_get_results()->width==(i==1u?3840u:7680u) && film_hevc_get_results()->codec_result==FILM_HEVC_CODEC_NOT_CALLED);}
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
    reset();
    printf("Native HEVC: real codec/CRC, %u cancellation+deadline guards, publication, races, allocations and I/O faults PASS\n",baseline_guards);
    return 0;
}
