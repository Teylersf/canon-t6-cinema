#!/usr/bin/env python3
"""Offline tests of the actual patched enumeration and Connect functions."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
MLINSTALL_PIN = "1c78f2639ac8d98b63c296592b6492f6597b84af"
LIBPICT_PIN = "2506c238ea0053d5cd8b12d5615077f8550b0c69"
LIBWPD_GITLINK_PIN = "bd96e1c06186cf784a4c02f03f11b364d38a8da8"
LIBWPD_BUILD_PIN = "1b523de4df5fdda15db2ac74ef176c227d668299"


def function(text, signature):
    start = text.index(signature + " {")
    return text[start:text.index("\n}\n", start) + 3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    for path, pin in ((source, MLINSTALL_PIN), (source / "libs/libpict", LIBPICT_PIN),
                      (source / "libs/libwpd", LIBWPD_GITLINK_PIN)):
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        assert actual == pin, "Unexpected source revision: " + str(path)
    wpd_repository = source / "libs/libwpd"
    def wpd_blob(pin, name):
        return subprocess.check_output(["git", "-C", str(wpd_repository), "show", pin + ":" + name])
    # Preserve the parent's gitlink checkout while testing the exact archive
    # selected for the GUI build. The original gitlink lacks wpd_close.
    assert wpd_blob(LIBWPD_BUILD_PIN, "src/parse.c") == wpd_blob(LIBWPD_GITLINK_PIN, "src/parse.c")
    assert b"void wpd_close(" in wpd_blob(LIBWPD_BUILD_PIN, "libwpd.h")
    assert b"void wpd_close(" in wpd_blob(LIBWPD_BUILD_PIN, "src/main.cpp")
    with tempfile.TemporaryDirectory(prefix="t6-mlinstall-test-") as directory:
        stage = Path(directory)
        for name in ("src/main.c", "libs/libpict/src/libwpd.c", "libs/libwpd/src/parse.c", "libs/libwpd/libwpd.h"):
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if name.startswith("libs/libpict/"):
                repository, pin, relative = source / "libs/libpict", LIBPICT_PIN, name[len("libs/libpict/"):]
            elif name.startswith("libs/libwpd/"):
                repository, pin, relative = source / "libs/libwpd", LIBWPD_BUILD_PIN, name[len("libs/libwpd/"):]
            else:
                repository, pin, relative = source, MLINSTALL_PIN, name
            target.write_bytes(subprocess.check_output(["git", "-C", str(repository), "show", pin + ":" + relative]))
        subprocess.run(["git", "apply", "--check", str(HERE / "skip-unsupported-wpd.patch")], cwd=stage, check=True)
        subprocess.run(["git", "apply", str(HERE / "skip-unsupported-wpd.patch")], cwd=stage, check=True)
        enumerator = function((stage / "libs/libpict/src/libwpd.c").read_text(),
                              "struct PtpDeviceEntry *ptpusb_device_list(struct PtpRuntime *r)")
        connect = function((stage / "src/main.c").read_text(), "int mlinstall_connect(void)")
        assert "ptp_panic" not in enumerator
        assert enumerator.index("wpd_parse_io_path") < enumerator.index("calloc")
        assert re.search(r"ptp_device_open\(ptp_runtime, selected\);\s*if \(rc\) return rc;", connect)
        runner = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "libwpd.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"mlinstall patch FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define USB_VENDOR_CANON 0x04a9
#define PTP_NO_DEVICE -3
struct PtpDeviceEntry {struct PtpDeviceEntry *prev;int id,vendor_id,product_id;uint32_t endpoint_in,endpoint_out,endpoint_int;char name[16],manufacturer[16];void *device_handle_ptr;struct PtpDeviceEntry *next;};
struct PtpRuntime {void *comm_backend;void *di;};
static struct PtpRuntime runtime,*ptp_runtime=&runtime;
static struct WpdStruct backend;
static const wchar_t *const *paths;
static int path_count,locks,allocated,freed,list_frees,fail_at,alloc_calls;
static int open_calls,session_calls,info_calls,disconnect_calls,open_rc,session_rc,info_rc,attempts;
static void *node_allocations[16];
static wchar_t **raw_list;
static int raw_count;
int ptp_comm_init(struct PtpRuntime *r){r->comm_backend=&backend;return 0;}
static void ptp_mutex_lock(struct PtpRuntime *r){CHECK(r==&runtime && locks==0);locks++;}
static void ptp_mutex_unlock(struct PtpRuntime *r){CHECK(r==&runtime && locks==1);locks--;}
wchar_t **wpd_get_devices(struct WpdStruct *w,int *count){int i;CHECK(w==&backend && locks==1);*count=path_count;raw_count=path_count;raw_list=malloc((size_t)(path_count?path_count:1)*sizeof(*raw_list));CHECK(raw_list);for(i=0;i<path_count;i++){size_t n=wcslen(paths[i])+1;raw_list[i]=malloc(n*sizeof(wchar_t));CHECK(raw_list[i]);memcpy(raw_list[i],paths[i],n*sizeof(wchar_t));}return raw_list;}
void wpd_free_devices(struct WpdStruct *w,wchar_t **list,int count){int i;CHECK(w==&backend && list==raw_list && count==raw_count);for(i=0;i<count;i++)free(list[i]);free(list);raw_list=NULL;list_frees++;}
static void *entry_calloc(size_t n,size_t size){void *p;CHECK(locks==1);alloc_calls++;if(fail_at && alloc_calls==fail_at)return NULL;p=calloc(n,size);CHECK(p);CHECK(allocated<16);node_allocations[allocated++]=p;return p;}
static void ptpusb_free_device_list(struct PtpDeviceEntry *entry){while(entry){struct PtpDeviceEntry *next=entry->next;free(entry);freed++;entry=next;}}
#define calloc entry_calloc
/* ACTUAL_ENUMERATOR */
#undef calloc
static void ptp_reset(struct PtpRuntime *r){CHECK(r==&runtime);}
static int ptp_device_open(struct PtpRuntime *r,struct PtpDeviceEntry *entry){CHECK(r==&runtime && entry && entry->vendor_id==USB_VENDOR_CANON);open_calls++;return open_rc;}
static int ptp_open_session(struct PtpRuntime *r){CHECK(r==&runtime);session_calls++;return session_rc;}
static int ptp_get_device_info(struct PtpRuntime *r,void *di){CHECK(r==&runtime && di==runtime.di);info_calls++;return info_rc;}
static int mlinstall_disconnect(void){disconnect_calls++;return 0;}
static void log_print(const char *fmt,...){(void)fmt;}
#define T_CANON_NOT_FOUND_FMT "No Canon (%d)"
static int quiet_printf(const char *fmt,...){(void)fmt;return 0;}
#define printf quiet_printf
/* ACTUAL_CONNECT */
#undef printf
static void reset(const wchar_t *const *items,int count){CHECK(!locks && !raw_list);paths=items;path_count=count;allocated=freed=list_frees=fail_at=alloc_calls=0;open_calls=session_calls=info_calls=disconnect_calls=0;open_rc=session_rc=info_rc=attempts=0;}
static void release_success_list(struct PtpDeviceEntry *head){ptpusb_free_device_list(head);if(raw_list)wpd_free_devices(&backend,raw_list,raw_count);CHECK(allocated==freed && !locks);}
static void release_connect_list(void){int i;for(i=0;i<allocated;i++)free(node_allocations[i]);freed=allocated;if(raw_list)wpd_free_devices(&backend,raw_list,raw_count);CHECK(!locks);}
static void check_order(const wchar_t *const *items,int count,const unsigned *vendors,int expected){int i=0;struct PtpDeviceEntry *previous=NULL,*head,*current;reset(items,count);head=ptpusb_device_list(&runtime);for(current=head;current;current=current->next){CHECK(i<expected && current->prev==previous && current->vendor_id==(int)vendors[i]);CHECK(current->device_handle_ptr && wcslen(current->device_handle_ptr)>0);previous=current;i++;}CHECK(i==expected && !locks);if(!expected)CHECK(!head && list_frees==1);else CHECK(list_frees==0);release_success_list(head);}
int main(void){
 const wchar_t *bad=L"\\\\?\\swd#wpdbusenum#synthetic-portable-entry";
 const wchar_t *canon=L"\\\\?\\usb#vid_04a9&pid_32b4#synthetic-camera";
 const wchar_t *other=L"\\\\?\\usb#vid_1234&pid_5678#synthetic-device";
 const wchar_t *items[5];const unsigned one[]={0x04a9},two[]={0x1234,0x04a9};
 struct LibWPDDescriptor d;
 CHECK(wpd_parse_io_path(canon,&d)==0 && d.vendor_id==0x04a9 && d.product_id==0x32b4);
 CHECK(wpd_parse_io_path(bad,&d)!=0);
 items[0]=bad;items[1]=canon;check_order(items,2,one,1);
 items[0]=other;items[1]=bad;items[2]=canon;check_order(items,3,two,2);
 items[0]=canon;items[1]=bad;check_order(items,2,one,1);
 items[0]=bad;items[1]=bad;check_order(items,2,NULL,0);
 items[0]=canon;check_order(items,1,one,1);
 check_order(items,0,NULL,0);
 items[0]=bad;items[1]=other;items[2]=canon;
 reset(items,3);fail_at=1;CHECK(ptpusb_device_list(&runtime)==NULL);CHECK(!locks && allocated==freed && list_frees==1);
 reset(items,3);fail_at=2;CHECK(ptpusb_device_list(&runtime)==NULL);CHECK(!locks && allocated==freed && list_frees==1);
 /* Exercise actual Connect branches using only mocks. */
 reset(items,3);open_rc=77;CHECK(mlinstall_connect()==77);CHECK(open_calls==1 && !session_calls && !info_calls && !disconnect_calls && !locks);
 CHECK(raw_list!=NULL);release_connect_list();
 reset(items,3);session_rc=66;CHECK(mlinstall_connect()==66);CHECK(open_calls==1 && session_calls==1 && !info_calls && !disconnect_calls);release_connect_list();
 reset(items,3);info_rc=55;CHECK(mlinstall_connect()==55);CHECK(open_calls==1 && session_calls==1 && info_calls==1 && disconnect_calls==1);release_connect_list();
 reset(items,3);CHECK(mlinstall_connect()==0);CHECK(open_calls==1 && session_calls==1 && info_calls==1 && !disconnect_calls);release_connect_list();
 items[0]=bad;items[1]=bad;reset(items,2);CHECK(mlinstall_connect()==PTP_NO_DEVICE);CHECK(!open_calls && !session_calls && !info_calls && !raw_list && list_frees==1);
 puts("mlinstall actual patched source: unsupported first/middle/last/all, USB order, allocation cleanup, open-error stop passed");
 return 0;
}
'''.replace("/* ACTUAL_ENUMERATOR */", enumerator).replace("/* ACTUAL_CONNECT */", connect)
        target = stage / "test.c"
        target.write_text(runner)
        flags = ["-std=c99", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-D__declspec(x)="]
        if args.sanitize:
            flags += ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
        binary = stage / "test"
        subprocess.run([shutil.which("cc") or "gcc", *flags, "-I" + str(stage / "libs/libwpd"),
                        str(target), str(stage / "libs/libwpd/src/parse.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
