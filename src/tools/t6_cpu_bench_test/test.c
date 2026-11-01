/* Actual synthetic CPU benchmark with mocked scheduling/FIO, no devices.
 * GPL-2.0-only. Host durations are injected; they are not CPU measurements. */
#include "dryos.h"
#include "fio-ml.h"
#include "mem.h"
#include "lens.h"
#include "propvalues.h"
#include "film_t6.h"
#include "film_cpu_bench.h"
#include "yuv_anamorphic.h"
#include <stdarg.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#define CSV_ONE "ML/FILM/CPUB0001.CSV"
#define TMP_ONE "ML/FILM/CPUB0001.TMP"
#define REPORT_CAPACITY 2048u
#define BIG_ALLOCATION 615680u

typedef struct {
    char path[64];
    uint8_t data[REPORT_CAPACITY];
    size_t size;
    int exists, ours;
} mock_file;
typedef struct {
    mock_file *file;
    size_t offset;
    int writing, open;
} mock_handle;
static mock_file files[40];
static mock_handle handles[8];
static void *big_pointer;
static int big_live, allocations, frees, kernel_calls, sleeps, fio_calls;
static void *verify_pointer;
static int verify_live, verify_allocations, verify_frees, verify_failed_allocations, fail_verify_allocation;
static int writes, reads, renames, removals, exclusive_opens;
static unsigned clock_ms;
static const uint32_t durations[12] = {5,7,3,9, 4,12,11,10, 6,2,5,4};
static int cancel_now, half_pressed, movie_mode;
static int fail_allocation, fail_kernel_at, cancel_at, shutdown_at;
static int capture_at, half_at, lv_at, movie_at, deadline_at;
static int reject_flags, race_tmp, race_csv, fail_write, fail_read;
static int corrupt_read, truncate_close, fail_rename, wrong_size;
static int guard_calls, guard_at, guard_kind, yield_kind, touched_before_free;
static int corrupt_csv_read, wrong_csv_size, fail_csv_open, zero_csv_read, grow_on_read;
static int expected_deadline_csv, report_stop_phase, report_stop_kind, report_stop_done;
static unsigned report_clock_advance;
static int expected_fio_after_calls;
static char status_text[100];
int ml_shutdown_requested, lv;
struct mock_lens_info lens_info;
static void inject_guard(int kind);

static mock_file *find_file(const char *path)
{
    unsigned i;
    for (i=0; i<40; ++i) if (files[i].exists && !strcmp(files[i].path,path)) return &files[i];
    return NULL;
}

static mock_file *add_file(const char *path, const void *data, size_t size, int ours)
{
    unsigned i;
    mock_file *file = find_file(path);
    CHECK(size <= REPORT_CAPACITY);
    if (!file) for (i=0; i<40; ++i) if (!files[i].exists) {file=&files[i];break;}
    CHECK(file != NULL);
    memset(file,0,sizeof(*file));
    CHECK(strlen(path)<sizeof(file->path));
    memcpy(file->path,path,strlen(path)+1u);
    if (size) memcpy(file->data,data,size);
    file->size=size;file->exists=1;file->ours=ours;
    return file;
}

static void path_checked(const char *path)
{
    unsigned number;
    char suffix[4];
    CHECK(path && strlen(path)==20);
    CHECK(sscanf(path,"ML/FILM/CPUB%4u.%3s",&number,suffix)==2);
    CHECK(number>=1 && number<=16);
    CHECK(!strcmp(suffix,"CSV") || !strcmp(suffix,"TMP"));
}

static void report_io(void)
{
    CHECK(!big_live && big_pointer==NULL);
    CHECK(kernel_calls==expected_fio_after_calls);
    ++fio_calls;
    clock_ms+=report_clock_advance;
}

static void report_stop(unsigned phase)
{
    if(report_stop_phase==(int)phase && !report_stop_done) {
        report_stop_done=1;inject_guard(report_stop_kind);
    }
}

static FILE *open_handle(mock_file *file, int writing)
{
    unsigned i;
    for (i=0;i<8;++i) if(!handles[i].open) {
        handles[i].file=file;handles[i].offset=0;handles[i].writing=writing;handles[i].open=1;
        return (FILE *)&handles[i];
    }
    CHECK(0);return NULL;
}

unsigned get_ms_clock(void) { return clock_ms; }
void msleep(int ms)
{
    CHECK(ms==1);clock_ms+=(unsigned)ms;++sleeps;
    if(yield_kind) {int kind=yield_kind;yield_kind=0;inject_guard(kind);}
}
int get_halfshutter_pressed(void) {return half_pressed;}
int is_movie_mode(void) {return movie_mode;}
static void inject_guard(int kind)
{
    switch(kind) {
    case 1:cancel_now=1;break;case 2:ml_shutdown_requested=1;break;
    case 3:lens_info.job_state=1;break;case 4:half_pressed=1;break;
    case 5:lv=1;break;case 6:movie_mode=1;break;
    case 7:clock_ms+=10000u;break;default:CHECK(0);break;
    }
}
int film_t6_cancelled(void)
{
    ++guard_calls;
    if(guard_at==guard_calls)inject_guard(guard_kind);
    return cancel_now || ml_shutdown_requested;
}
void film_t6_set_status(const char *text) {snprintf(status_text,sizeof(status_text),"%s",text);}

void *fio_malloc(size_t size)
{
    CHECK(!big_live && big_pointer==NULL && !verify_live && verify_pointer==NULL);
    if(size==BIG_ALLOCATION) {
        ++allocations;
        if(fail_allocation)return NULL;
        big_pointer=malloc(size);CHECK(big_pointer);
        memset(big_pointer,0xc7,size);
        big_live=1;return big_pointer;
    }
    CHECK(size==64u);
    CHECK(kernel_calls==expected_fio_after_calls);
    ++verify_allocations;
    if(fail_verify_allocation==verify_allocations) {
        ++verify_failed_allocations;return NULL;
    }
    verify_pointer=malloc(size);CHECK(verify_pointer);
    memset(verify_pointer,0xc7,size);
    verify_live=1;return verify_pointer;
}

void fio_free(void *pointer)
{
    if(big_live && pointer==big_pointer) {
        CHECK(!verify_live && verify_pointer==NULL);
        touched_before_free=((const uint8_t *)pointer)[0]!=0xc7;
        free(pointer);big_pointer=NULL;big_live=0;++frees;
        return;
    }
    CHECK(verify_live && pointer==verify_pointer && !big_live && big_pointer==NULL);
    free(pointer);verify_pointer=NULL;verify_live=0;++verify_frees;
}

int is_file(const char *path)
{
    report_io();path_checked(path);return find_file(path)!=NULL;
}

FILE *FIO_OpenFile(const char *path,unsigned flags)
{
    mock_file *file;
    report_io();path_checked(path);file=find_file(path);
    if(flags==O_RDONLY) {
        if(fail_csv_open && strstr(path,".CSV"))return NULL;
        return file ? open_handle(file,0):NULL;
    }
    /* Independent native 1300D.110 contract: no macro alias or fallback. */
    CHECK(flags==0x8501u);
    CHECK(strstr(path,".TMP")!=NULL);
    ++exclusive_opens;
    if(race_tmp && !file)file=add_file(path,"KEEPTEMP",8,0);
    if(reject_flags || file)return NULL;
    {
        FILE *result=open_handle(add_file(path,NULL,0,1),1);
        report_stop(1);
        return result;
    }
}

int FIO_WriteFile(FILE *stream,const void *source,size_t count)
{
    mock_handle *handle=(mock_handle *)stream;
    size_t actual=count;
    report_io();CHECK(handle && handle->open && handle->writing && handle->file->ours);
    CHECK(count>0 && count<=REPORT_CAPACITY);
    ++writes;
    if(fail_write==1)return -1;
    if(fail_write==2)actual=count-1u;
    if(fail_write==3)return 0;
    CHECK(handle->offset+actual<=REPORT_CAPACITY);
    memcpy(handle->file->data+handle->offset,source,actual);
    handle->offset+=actual;handle->file->size=handle->offset;
    report_stop(2);
    return (int)actual;
}

int FIO_ReadFile(FILE *stream,void *destination,size_t count)
{
    mock_handle *handle=(mock_handle *)stream;
    size_t actual=count;
    report_io();CHECK(handle && handle->open && !handle->writing);
    /* Native FIO must receive the owned fio_malloc buffer, never stack/cache
     * memory that would select its separate bounce/assert path. */
    CHECK(verify_live && verify_pointer!=NULL);
    CHECK((uintptr_t)destination>=(uintptr_t)verify_pointer);
    CHECK((uintptr_t)destination-(uintptr_t)verify_pointer<=64u);
    CHECK(count<=64u-((uintptr_t)destination-(uintptr_t)verify_pointer));
    CHECK(handle->offset<=handle->file->size);
    ++reads;
    if(zero_csv_read && strstr(handle->file->path,".CSV"))return 0;
    if(fail_read==1)return -1;
    if(fail_read==2 && actual>1)actual--;
    if(fail_read==3)return 0;
    if(actual>handle->file->size-handle->offset)actual=handle->file->size-handle->offset;
    memcpy(destination,handle->file->data+handle->offset,actual);
    handle->offset+=actual;
    if(corrupt_read && actual)((uint8_t *)destination)[actual-1u]^=1;
    if(corrupt_csv_read && actual && strstr(handle->file->path,".CSV"))
        ((uint8_t *)destination)[actual-1u]^=1;
    if(grow_on_read && actual && handle->file->size<REPORT_CAPACITY) {
        handle->file->data[handle->file->size++]=0x2a;
        grow_on_read=0;
    }
    if(strstr(handle->file->path,".TMP"))report_stop(3);
    return (int)actual;
}

void FIO_CloseFile(FILE *stream)
{
    mock_handle *handle=(mock_handle *)stream;
    report_io();CHECK(handle && handle->open);
    /* The real API is void. Model silent loss at writer close, detected by
     * subsequent size/readback checks rather than a fictional close return. */
    if(truncate_close && handle->writing && handle->file->size)handle->file->size--;
    if(!handle->writing && strstr(handle->file->path,".TMP"))report_stop(4);
    handle->open=0;
}

int FIO_GetFileSize(const char *path,uint32_t *size)
{
    mock_file *file;
    report_io();path_checked(path);file=find_file(path);
    if(!file)return -1;
    *size=(uint32_t)file->size;
    if(wrong_size && file->ours && file->size)(*size)++;
    if(wrong_csv_size && strstr(path,".CSV") && file->ours && file->size)(*size)++;
    return 0;
}

int FIO_RenameFile(const char *source,const char *destination)
{
    mock_file *file;
    report_io();path_checked(source);path_checked(destination);
    CHECK(strstr(source,".TMP") && strstr(destination,".CSV"));
    file=find_file(source);CHECK(file && file->ours);
    if(race_csv && !find_file(destination))add_file(destination,"KEEPCSV!",8,0);
    if(fail_rename || find_file(destination))return -1;
    snprintf(file->path,sizeof(file->path),"%s",destination);
    ++renames;return 0;
}

int FIO_RemoveFile(const char *path)
{
    mock_file *file;
    report_io();path_checked(path);CHECK(strstr(path,".TMP"));
    file=find_file(path);CHECK(file && file->ours);
    file->exists=0;++removals;return 0;
}

static void check_fresh_synthetic_source(const se_ya_frame *frame)
{
    uint32_t y;
    size_t x;
    for(y=0;y<frame->height;++y) {
        unsigned highlights=0;
        const uint8_t *row=frame->data+(size_t)y*frame->stride;
        for(x=0;x<(size_t)frame->width*2u;x+=4u) {
            CHECK(row[x]>=16 && row[x]<=235 && row[x+2u]>=16 && row[x+2u]<=235);
            CHECK(row[x+1u]==128 && row[x+3u]==128);
            if(row[x]>=230 || row[x+2u]>=230)highlights++;
        }
        CHECK(highlights>0);
    }
}

static se_ya_result counted_kernel(const se_ya_frame *frame,const se_ya_params *params,
                                  void *scratch,size_t scratch_size)
{
    static const uint32_t widths[3]={320,640,640}, heights[3]={180,360,480};
    se_ya_params recipe;
    se_ya_result result;
    unsigned index=(unsigned)kernel_calls, which=index/4u;
    CHECK(big_live && allocations==1 && frees==0 && index<12);
    CHECK(frame->data==big_pointer && scratch==(uint8_t *)big_pointer+614400u);
    CHECK(scratch_size==1280u && frame->bytes<=614400u);
    CHECK(frame->width==widths[which] && frame->height==heights[which]);
    CHECK(frame->stride==(size_t)frame->width*2u);
    CHECK(frame->layout==SE_YA_YUYV && frame->range==SE_YA_STUDIO_RANGE);
    CHECK(frame->chroma==SE_YA_CHROMA_UNSIGNED_128 && frame->ownership==SE_YA_EXCLUSIVE_CPU);
    CHECK(se_ya_preset(&recipe,SE_YA_STUDIO_RANGE,3,0)==SE_YA_OK);
    CHECK(params->strength_q8==recipe.strength_q8 && params->gain==recipe.gain);
    CHECK(params->threshold==recipe.threshold && params->tint_cb==recipe.tint_cb && params->tint_cr==recipe.tint_cr);
    CHECK(params->tint_strength_q8==recipe.tint_strength_q8);
    CHECK(params->short_decay_shift==recipe.short_decay_shift && params->long_decay_shift==recipe.long_decay_shift);
    check_fresh_synthetic_source(frame);
    ++kernel_calls;
    result=fail_kernel_at==kernel_calls ? SE_YA_BAD_DESCRIPTOR : se_ya_apply(frame,params,scratch,scratch_size);
    clock_ms+=durations[index];
    if(cancel_at==kernel_calls)cancel_now=1;
    if(shutdown_at==kernel_calls)ml_shutdown_requested=1;
    if(capture_at==kernel_calls)lens_info.job_state=1;
    if(half_at==kernel_calls)half_pressed=1;
    if(lv_at==kernel_calls)lv=1;
    if(movie_at==kernel_calls)movie_mode=1;
    if(deadline_at==kernel_calls)clock_ms+=10000u;
    return result;
}

static int checked_snprintf(char *destination,size_t size,const char *format,...)
{
    const char *p=format;
    va_list args;
    int result;
    while(*p) {
        if(*p++!='%')continue;
        if(*p=='%'){++p;continue;}
        if(!strncmp(p,"04d",3)){p+=3;continue;}
        CHECK(*p=='s' || *p=='d');++p;
    }
    va_start(args,format);result=vsnprintf(destination,size,format,args);va_end(args);return result;
}

#define se_ya_apply counted_kernel
#define snprintf checked_snprintf
#include "../../../integrations/magiclantern/1300D.110/src/film_cpu_bench.c"
#undef snprintf
#undef se_ya_apply

static void reset(void)
{
    CHECK(!big_live && big_pointer==NULL && !verify_live && verify_pointer==NULL);
    memset(files,0,sizeof(files));memset(handles,0,sizeof(handles));
    status_text[0]=0;
    allocations=frees=kernel_calls=sleeps=fio_calls=0;
    verify_allocations=verify_frees=verify_failed_allocations=fail_verify_allocation=0;
    writes=reads=renames=removals=exclusive_opens=0;
    clock_ms=0;cancel_now=half_pressed=movie_mode=lv=ml_shutdown_requested=0;
    lens_info.job_state=0;
    fail_allocation=fail_kernel_at=cancel_at=shutdown_at=capture_at=half_at=lv_at=movie_at=deadline_at=0;
    reject_flags=race_tmp=race_csv=fail_write=fail_read=corrupt_read=truncate_close=fail_rename=wrong_size=0;
    guard_calls=guard_at=guard_kind=yield_kind=touched_before_free=0;
    corrupt_csv_read=wrong_csv_size=fail_csv_open=zero_csv_read=grow_on_read=0;
    expected_deadline_csv=report_stop_phase=report_stop_kind=report_stop_done=0;
    report_clock_advance=0;
    expected_fio_after_calls=12;
}

static void no_handles_or_allocations(void)
{
    unsigned i;
    CHECK(!big_live && big_pointer==NULL);
    CHECK(!verify_live && verify_pointer==NULL);
    for(i=0;i<8;++i)CHECK(!handles[i].open);
    CHECK(frees==allocations || (fail_allocation && frees==0));
    CHECK(verify_frees+verify_failed_allocations==verify_allocations);
}

static void preserved(const char *path,const char *value)
{
    mock_file *file=find_file(path);
    CHECK(file && file->size==strlen(value) && !memcmp(file->data,value,file->size));
}

static void assert_csv(const char *path)
{
    mock_file *file=find_file(path);
    const struct film_cpu_results *results=film_cpu_bench_get_results();
    char text[REPORT_CAPACITY+1u],*line;
    unsigned i,j;
    long long values[10];
    CHECK(file && file->ours && file->size>0 && file->size<=REPORT_CAPACITY);
    CHECK(!memchr(file->data,0,file->size));
    memcpy(text,file->data,file->size);text[file->size]=0;
    CHECK(strstr(text,"type,synthetic_cpu_kernel_only\n"));
    CHECK(strstr(text,"layout,YUYV\n") && strstr(text,"chroma,unsigned128\n"));
    CHECK(strstr(text,"range,studio_Y16_235_C16_240\n") && strstr(text,"fills_timed,0\n"));
    CHECK(strstr(text,"encoder_measured,0\n") && strstr(text,"recording_fps_measured,0\n"));
    CHECK(strstr(text,"unique_exposures_measured,0\n") && strstr(text,"actual_camera_format_known,0\n"));
    CHECK(strstr(text,"timed_per_case_requested,3\n"));
    CHECK(strstr(text,expected_deadline_csv ? "status,deadline\n":"status,complete\n"));
    line=strstr(text,"case,width,height,warmup,completed_frames,ms1,ms2,ms3,min_ms,max_ms");
    CHECK(line);line=strchr(line,'\n');CHECK(line);++line;
    for(i=0;i<3;++i) {
        const struct film_cpu_case *item=&results->cases[i];
        CHECK(sscanf(line,"%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
              &values[0],&values[1],&values[2],&values[3],&values[4],
              &values[5],&values[6],&values[7],&values[8],&values[9])==10);
        CHECK(values[0]==i && values[1]==item->width && values[2]==item->height);
        CHECK(values[3]==item->warmup_done && values[4]==item->count);
        for(j=0;j<3;++j)CHECK(values[5u+j]==(j<item->count ? (long long)item->milliseconds[j]:-1ll));
        CHECK(values[8]==(item->count ? (long long)item->minimum_ms:-1ll));
        CHECK(values[9]==(item->count ? (long long)item->maximum_ms:-1ll));
        line=strchr(line,'\n');CHECK(line);++line;
    }
}

static void test_success_and_wrap(void)
{
    unsigned wrap,i,j;
    for(wrap=0;wrap<2;++wrap) {
        unsigned started;
        const struct film_cpu_results *results;
        reset();if(wrap)clock_ms=UINT32_MAX-20u;started=clock_ms;
        CHECK(film_cpu_bench()==0);results=film_cpu_bench_get_results();
        CHECK(results->state==FILM_CPU_SAVED && results->cases_completed==3);
        CHECK(results->version==1);
        CHECK(results->elapsed_ms==clock_ms-started);
        CHECK(kernel_calls==12 && sleeps>=11 && sleeps<=12);
        CHECK(allocations==1 && frees==1 && writes>0 && reads>0 && renames==1 && removals==0);
        CHECK(verify_allocations==2 && verify_frees==2 && !verify_failed_allocations);
        CHECK(!strcmp(results->report_path,CSV_ONE));
        for(i=0;i<3;++i) {
            const struct film_cpu_case *item=&results->cases[i];
            uint32_t minimum=UINT32_MAX,maximum=0;
            CHECK(item->warmup_done==1 && item->count==3);
            for(j=0;j<3;++j) {
                uint32_t duration=durations[i*4u+j+1u];
                CHECK(item->milliseconds[j]==duration);
                if(duration<minimum)minimum=duration;
                if(duration>maximum)maximum=duration;
            }
            CHECK(item->minimum_ms==minimum && item->maximum_ms==maximum);
        }
        assert_csv(CSV_ONE);no_handles_or_allocations();
    }
}

static void assert_partial_progress(unsigned completed_calls)
{
    static const uint32_t widths[3]={320,640,640},heights[3]={180,360,480};
    const struct film_cpu_results *results=film_cpu_bench_get_results();
    unsigned c,j,complete=0;
    for(c=0;c<3;++c) {
        const struct film_cpu_case *item=&results->cases[c];
        unsigned available=completed_calls>c*4u ? completed_calls-c*4u:0;
        unsigned count=available>1u ? available-1u:0;
        uint32_t minimum=UINT32_MAX,maximum=0;
        if(count>3)count=3;
        CHECK(item->width==widths[c] && item->height==heights[c]);
        CHECK(item->warmup_done==(available>0) && item->count==count);
        for(j=0;j<count;++j) {
            unsigned call=c*4u+2u+j;
            uint32_t expected=durations[call-1u]+(deadline_at==(int)call ? 10000u:0u);
            CHECK(item->milliseconds[j]==expected);
            if(expected<minimum)minimum=expected;
            if(expected>maximum)maximum=expected;
        }
        if(count)CHECK(item->minimum_ms==minimum && item->maximum_ms==maximum);
        if(count==3)complete++;
    }
    CHECK(results->cases_completed==complete);
}

static void test_cancel_busy_and_kernel(void)
{
    unsigned injection,call;
    for(injection=0;injection<7;++injection)
        for(call=1;call<=12;++call) {
            const struct film_cpu_results *results;
            reset();
            switch(injection) {
            case 0:cancel_at=(int)call;break;
            case 1:shutdown_at=(int)call;break;
            case 2:capture_at=(int)call;break;
            case 3:half_at=(int)call;break;
            case 4:lv_at=(int)call;break;
            case 5:movie_at=(int)call;break;
            default:deadline_at=(int)call;break;
            }
            if(injection==6) {
                expected_fio_after_calls=(int)call;expected_deadline_csv=1;
            }
            CHECK(film_cpu_bench()==(injection==6 ? 0:1));results=film_cpu_bench_get_results();
            CHECK(kernel_calls==(int)call);
            if(injection==6) {
                CHECK(fio_calls>0 && !strcmp(results->report_path,CSV_ONE));assert_csv(CSV_ONE);
            } else CHECK(fio_calls==0 && !results->report_path[0]);
            CHECK(results->state==(injection<2 ? FILM_CPU_CANCELLED : injection==6 ? FILM_CPU_DEADLINE : FILM_CPU_BUSY));
            CHECK(results->cases[(call-1u)/4u].warmup_done==1);
            CHECK(results->cases[(call-1u)/4u].count==(call-1u)%4u);
            if((call-1u)%4u)
                CHECK(results->cases[(call-1u)/4u].milliseconds[(call-1u)%4u-1u]==durations[call-1u]+(injection==6 ? 10000u:0u));
            assert_partial_progress(call);
            no_handles_or_allocations();
        }
    for(call=1;call<=12;++call) {
        reset();fail_kernel_at=(int)call;
        CHECK(film_cpu_bench()==-1 && kernel_calls==(int)call && fio_calls==0);
        CHECK(film_cpu_bench_get_results()->state==FILM_CPU_KERNEL_ERROR);
        no_handles_or_allocations();
    }
    for(injection=0;injection<6;++injection) {
        reset();
        switch(injection) {
        case 0:cancel_now=1;break;case 1:ml_shutdown_requested=1;break;
        case 2:lens_info.job_state=1;break;case 3:half_pressed=1;break;
        case 4:lv=1;break;default:movie_mode=1;break;
        }
        CHECK(film_cpu_bench()==1 && kernel_calls==0 && fio_calls==0);
        no_handles_or_allocations();
    }
    reset();fail_allocation=1;
    CHECK(film_cpu_bench()==-1 && kernel_calls==0 && fio_calls==0);
    CHECK(film_cpu_bench_get_results()->state==FILM_CPU_NO_MEMORY);
    no_handles_or_allocations();
}

static void test_guard_boundaries(void)
{
    unsigned kind,boundary;
    for(kind=1;kind<=7;++kind)
        for(boundary=1;boundary<=3;++boundary) {
            reset();guard_at=(int)boundary;guard_kind=(int)kind;
            if(kind==7) {expected_fio_after_calls=0;expected_deadline_csv=1;}
            CHECK(film_cpu_bench()==(kind==7 ? 0:1));
            CHECK(kernel_calls==0);
            if(kind==7) {CHECK(fio_calls>0);assert_csv(CSV_ONE);} else CHECK(fio_calls==0);
            CHECK(film_cpu_bench_get_results()->state==(kind<=2 ? FILM_CPU_CANCELLED : kind==7 ? FILM_CPU_DEADLINE : FILM_CPU_BUSY));
            CHECK(allocations==(boundary==1 ? 0 : 1));
            CHECK(touched_before_free==(boundary==3));
            assert_partial_progress(0);
            no_handles_or_allocations();
        }
    for(kind=1;kind<=7;++kind) {
        reset();yield_kind=(int)kind;
        if(kind==7) {expected_fio_after_calls=1;expected_deadline_csv=1;}
        CHECK(film_cpu_bench()==(kind==7 ? 0:1) && kernel_calls==1 && sleeps==1);
        if(kind==7) {CHECK(fio_calls>0);assert_csv(CSV_ONE);} else CHECK(fio_calls==0);
        CHECK(film_cpu_bench_get_results()->state==(kind<=2 ? FILM_CPU_CANCELLED : kind==7 ? FILM_CPU_DEADLINE : FILM_CPU_BUSY));
        CHECK(film_cpu_bench_get_results()->cases[0].warmup_done==1);
        CHECK(film_cpu_bench_get_results()->cases[0].count==0);
        assert_partial_progress(1);
        no_handles_or_allocations();
    }
}

static void test_deadline_precedence_and_report_stop(void)
{
    unsigned kind,call,phase,partial;
    /* A contemporaneous Stop/shutdown/capture/mode change overrides deadline,
     * keeping the interrupted frame's raw sample in RAM but never doing I/O. */
    for(kind=1;kind<=6;++kind)
        for(call=1;call<=12;++call) {
            reset();deadline_at=(int)call;
            switch(kind) {
            case 1:cancel_at=(int)call;break;case 2:shutdown_at=(int)call;break;
            case 3:capture_at=(int)call;break;case 4:half_at=(int)call;break;
            case 5:lv_at=(int)call;break;default:movie_at=(int)call;break;
            }
            CHECK(film_cpu_bench()==1 && kernel_calls==(int)call && fio_calls==0);
            CHECK(film_cpu_bench_get_results()->state==(kind<=2 ? FILM_CPU_CANCELLED:FILM_CPU_BUSY));
            CHECK(!film_cpu_bench_get_results()->report_path[0]);
            assert_partial_progress(call);no_handles_or_allocations();
        }
    /* Stop or capture activity at each file-creation/verification checkpoint
     * must close handles and remove only our exclusive-created TMP. */
    for(partial=0;partial<2;++partial)
        for(phase=1;phase<=4;++phase)
            for(kind=1;kind<=6;++kind) {
                reset();report_stop_phase=(int)phase;report_stop_kind=(int)kind;
                if(partial) {deadline_at=2;expected_fio_after_calls=2;expected_deadline_csv=1;}
                add_file("ML/FILM/CPUB0002.TMP","UNRELATED",9,0);
                CHECK(film_cpu_bench()==1 && report_stop_done);
                CHECK(film_cpu_bench_get_results()->state==(kind<=2 ? FILM_CPU_CANCELLED:FILM_CPU_BUSY));
                CHECK(!find_file(CSV_ONE) && !find_file(TMP_ONE));
                CHECK(renames==0 && removals==1);
                CHECK(!film_cpu_bench_get_results()->report_path[0]);
                preserved("ML/FILM/CPUB0002.TMP","UNRELATED");
                assert_partial_progress(partial ? 2u:12u);no_handles_or_allocations();
            }
    /* File I/O is outside the cooperative CPU budget, while CPU elapsed and
     * per-frame durations remain the samples collected before any report I/O. */
    reset();report_clock_advance=20000u;
    CHECK(film_cpu_bench()==0 && clock_ms>10000u);
    CHECK(film_cpu_bench_get_results()->state==FILM_CPU_SAVED);
    CHECK(film_cpu_bench_get_results()->elapsed_ms==90u);
    assert_csv(CSV_ONE);no_handles_or_allocations();
}

static void test_file_guards(void)
{
    unsigned variant,slot;
    const struct film_cpu_results *results;
    for(variant=0;variant<2;++variant) {
        reset();add_file(variant ? TMP_ONE : CSV_ONE,"EXISTING",8,0);
        CHECK(film_cpu_bench()==0);results=film_cpu_bench_get_results();
        CHECK(!strcmp(results->report_path,"ML/FILM/CPUB0002.CSV"));
        preserved(variant ? TMP_ONE : CSV_ONE,"EXISTING");
        assert_csv(results->report_path);no_handles_or_allocations();
    }
    reset();
    for(slot=1;slot<=16;++slot) {
        char path[32];snprintf(path,sizeof(path),"ML/FILM/CPUB%04u.CSV",slot);
        add_file(path,"KEEP",4,0);
    }
    CHECK(film_cpu_bench()==-1 && exclusive_opens==0 && removals==0);
    CHECK(film_cpu_bench_get_results()->state==FILM_CPU_REPORT_ERROR);
    no_handles_or_allocations();
    reset();race_tmp=1;
    CHECK(film_cpu_bench()==-1 && removals==0 && renames==0);
    preserved(TMP_ONE,"KEEPTEMP");no_handles_or_allocations();
    reset();race_csv=1;
    CHECK(film_cpu_bench()==-1 && removals==1 && renames==0);
    preserved(CSV_ONE,"KEEPCSV!");CHECK(!find_file(TMP_ONE));no_handles_or_allocations();
}

static void test_report_failures(void)
{
    unsigned variant;
    for(variant=0;variant<13;++variant) {
        reset();
        switch(variant) {
        case 0:reject_flags=1;break;
        case 1:fail_write=1;break;case 2:fail_write=2;break;case 3:fail_write=3;break;
        case 4:fail_read=1;break;case 5:fail_read=2;break;case 6:fail_read=3;break;
        case 7:corrupt_read=1;break;case 8:truncate_close=1;break;
        case 9:fail_rename=1;break;case 10:wrong_size=1;break;
        case 11:reject_flags=1;add_file("ML/FILM/CPUB0002.TMP","UNRELATED",9,0);break;
        default:grow_on_read=1;break;
        }
        if(variant==5) {
            /* Short positive reads may be retried; the complete roundtrip must
             * still compare and publish exactly the original report bytes. */
            CHECK(film_cpu_bench()==0);
            assert_csv(CSV_ONE);no_handles_or_allocations();continue;
        }
        CHECK(film_cpu_bench()==-1);
        CHECK(film_cpu_bench_get_results()->state==FILM_CPU_REPORT_ERROR);
        CHECK(!find_file(CSV_ONE) && !find_file(TMP_ONE) && renames==0);
        if(variant==11)preserved("ML/FILM/CPUB0002.TMP","UNRELATED");
        no_handles_or_allocations();
    }
    for(variant=0;variant<4;++variant) {
        reset();
        switch(variant) {
        case 0:corrupt_csv_read=1;break;case 1:wrong_csv_size=1;break;
        case 2:fail_csv_open=1;break;default:zero_csv_read=1;break;
        }
        CHECK(film_cpu_bench()==-1);
        CHECK(film_cpu_bench_get_results()->state==FILM_CPU_REPORT_ERROR);
        CHECK(find_file(CSV_ONE) && !find_file(TMP_ONE));
        CHECK(renames==1 && removals==0);
        CHECK(!film_cpu_bench_get_results()->report_path[0]);
        assert_csv(CSV_ONE);no_handles_or_allocations();
    }
    /* Either verification allocation may fail: before Rename only our TMP
     * is cleaned; after publication the completed CSV must remain intact. */
    for(variant=0;variant<4;++variant) {
        unsigned which=variant%2u+1u;
        reset();fail_verify_allocation=(int)which;
        if(variant>=2) {deadline_at=2;expected_fio_after_calls=2;expected_deadline_csv=1;}
        add_file("ML/FILM/CPUB0002.TMP","UNRELATED",9,0);
        CHECK(film_cpu_bench()==-1);
        CHECK(film_cpu_bench_get_results()->state==FILM_CPU_REPORT_ERROR);
        CHECK(verify_allocations==(int)which && verify_frees==(int)which-1 && verify_failed_allocations==1);
        CHECK(!find_file(TMP_ONE));
        CHECK(renames==(which==2) && removals==(which==1));
        if(which==1)CHECK(!find_file(CSV_ONE));else {CHECK(find_file(CSV_ONE));assert_csv(CSV_ONE);}
        preserved("ML/FILM/CPUB0002.TMP","UNRELATED");
        no_handles_or_allocations();
    }
}

int main(void)
{
    test_success_and_wrap();
    test_cancel_busy_and_kernel();
    test_guard_boundaries();
    test_deadline_precedence_and_report_stop();
    test_file_guards();
    test_report_failures();
    puts("T6 actual CPU benchmark: timing, ownership, cancellation and report fault tests passed");
    return 0;
}
