#include "card_layout.hpp"
#include "package_json.hpp"
#include <iostream>
#include <map>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#endif
using namespace t6card;
namespace {
unsigned checks=0;
void check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(std::string("TEST FAILED: ")+name);}
void put16(Sector& s,size_t at,uint16_t n){s[at]=uint8_t(n);s[at+1]=uint8_t(n>>8);}
void put32(Sector& s,size_t at,uint32_t n){put16(s,at,uint16_t(n));put16(s,at+2,uint16_t(n>>16));}
void sig(Sector& s){s[510]=0x55;s[511]=0xaa;}
struct Image {
    std::map<uint64_t,Sector> blocks;
    Context c;unsigned writes=0,flushes=0;int failWrite=-1,failFlush=-1;
    Image(){
        c.diskBytes=ExpectedBytes;c.extentStart=2048*512;c.extentBytes=ExpectedBytes-c.extentStart;c.bytesPerSector=512;
        c.removable=c.allowedBus=c.writable=c.oneExtent=c.onePartition=c.mbrStyle=c.online=true;c.systemDisk=false;c.volumeLabel="EOS_DIGITAL";
        Sector m{};sig(m);m[446+4]=0x0c;put32(m,454,2048);put32(m,458,static_cast<uint32_t>(c.extentBytes/512));blocks[0]=m;
        Sector b{};b[0]=0xeb;b[1]=0x58;b[2]=0x90;std::memcpy(b.data()+3,"MSDOS5.0",8);put16(b,11,512);b[13]=64;put16(b,14,32);b[16]=2;b[21]=0xf8;put32(b,28,2048);put32(b,32,static_cast<uint32_t>(c.extentBytes/512));put32(b,36,4096);put32(b,44,2);put16(b,48,1);put16(b,50,6);b[64]=0x80;b[66]=0x29;put32(b,67,0x13572468);std::memcpy(b.data()+71,"EOS_DIGITAL",11);std::memcpy(b.data()+82,"FAT32   ",8);sig(b);blocks[2048]=blocks[2054]=b;
        Sector f{};put32(f,0,0x41615252);put32(f,484,0x61417272);put32(f,488,0xffffffff);put32(f,492,0xffffffff);put32(f,508,0xaa550000);blocks[2049]=blocks[2055]=f;
        Sector fat{};put32(fat,0,0x0ffffff8);put32(fat,4,0xffffffff);put32(fat,8,0x0fffffff);put32(fat,12,0x0fffffff);blocks[2080]=blocks[6176]=fat;
        const uint64_t data=2048+32+8192;Sector dir{};std::memcpy(dir.data(),"AUTOEXECBIN",11);dir[11]=0x20;put16(dir,26,3);put32(dir,28,900);blocks[data]=dir;
        Sector a{},z{};for(size_t i=0;i<512;i++){a[i]=uint8_t(i);z[i]=uint8_t(i^0xa5);}blocks[data+64]=a;blocks[data+65]=z;
    }
    Sector read(uint64_t n)const{require(n<ExpectedBytes/512,"fixture out-of-disk read");const auto it=blocks.find(n);return it==blocks.end()?Sector{}:it->second;}
    Read reader(){return[this](uint64_t n){return read(n);};}
    void write(uint64_t n,const Sector& s){++writes;if(int(writes)==failWrite)throw std::runtime_error("injected write failure");blocks[n]=s;}
    void flush(){++flushes;if(int(flushes)==failFlush)throw std::runtime_error("injected flush failure");}
    void apply(const Layout& l){apply_markers(l,reader(),[this](uint64_t n,const Sector& s){write(n,s);},[this]{flush();});}
    void bpb(const std::function<void(Sector&)>& change){change(blocks[2048]);blocks[2054]=blocks[2048];}
};
void refusal(const char* name,const std::function<void(Image&)>& alter,bool autoexec=false){
    Image i;alter(i);const auto original=i.blocks;bool rejected=false;
    try{const Layout l=inspect(i.reader(),i.c);if(autoexec)(void)read_autoexec(i.reader(),l);else i.apply(l);}catch(const std::exception&){rejected=true;}
    check(rejected,name);check(i.writes==0,"preflight made no write calls");check(i.blocks==original,"entire sparse image unchanged on refusal");
}
void layout_tests(){
    Image i;const auto before=i.blocks;const Layout l=inspect(i.reader(),i.c);check(l.start==2048&&l.backup==6&&l.fsinfo==1,"valid FAT32 BPB parsed");
    const auto exe=read_autoexec(i.reader(),l);check(exe.size()==900&&exe[0]==0&&exe[511]==255&&exe[512]==0xa5,"valid multi-sector AUTOEXEC read");
    check(i.blocks==before&&i.writes==0,"inspection and raw-file read are inert");i.apply(l);check(i.writes==2&&i.flushes==2,"exactly two sector writes and flushes");
    for(const auto& pair:i.blocks){const uint64_t n=pair.first;if(n==2048||n==2054){check(pair.second==marked(before.at(n)),"only exact planned marker bytes differ");for(size_t b=0;b<512;b++)if(!(b>=0x47&&b<0x52)&&!(b>=0x5c&&b<0x64))check(pair.second[b]==before.at(n)[b],"unrelated bootsector byte preserved");}else check(pair.second==before.at(n),"nonboot sector preserved");}
    refusal("wrong exact device capacity",[](Image& x){x.c.diskBytes-=512;});
    refusal("system disk",[](Image& x){x.c.systemDisk=true;});
    refusal("readonly disk",[](Image& x){x.c.writable=false;});
    refusal("offline disk",[](Image& x){x.c.online=false;});
    refusal("fixed media",[](Image& x){x.c.removable=false;});
    refusal("internal bus",[](Image& x){x.c.allowedBus=false;});
    refusal("multiple extents",[](Image& x){x.c.oneExtent=false;});
    refusal("GPT layout",[](Image& x){x.c.mbrStyle=false;});
    refusal("multiple OS partition entries",[](Image& x){x.c.onePartition=false;});
    refusal("wrong volume label",[](Image& x){x.c.volumeLabel="DATA";});
    refusal("non512 physical sector",[](Image& x){x.c.bytesPerSector=4096;});
    refusal("out-of-disk extent",[](Image& x){x.c.extentBytes=UINT64_MAX;});
    refusal("unaligned extent",[](Image& x){++x.c.extentStart;});
    refusal("missing MBR signature",[](Image& x){x.blocks[0][510]=0;});
    refusal("second MBR partition",[](Image& x){x.blocks[0][466]=0x0c;});
    refusal("protective GPT MBR",[](Image& x){x.blocks[0][450]=0xee;});
    refusal("invalid boot indicator",[](Image& x){x.blocks[0][446]=1;});
    refusal("MBR start mismatch",[](Image& x){put32(x.blocks[0],454,4096);});
    refusal("MBR size mismatch",[](Image& x){put32(x.blocks[0],458,0xffffffff);});
    refusal("missing BPB signature",[](Image& x){x.bpb([](Sector& b){b[511]=0;});});
    refusal("invalid jump",[](Image& x){x.bpb([](Sector& b){b[0]=0;});});
    refusal("BPB bytes per sector",[](Image& x){x.bpb([](Sector& b){put16(b,11,4096);});});
    refusal("nonpower cluster size",[](Image& x){x.bpb([](Sector& b){b[13]=3;});});
    refusal("zero cluster size",[](Image& x){x.bpb([](Sector& b){b[13]=0;});});
    refusal("too few reserved sectors",[](Image& x){x.bpb([](Sector& b){put16(b,14,2);});});
    refusal("nonmirrored FAT",[](Image& x){x.bpb([](Sector& b){put16(b,40,0x80);});});
    refusal("FAT table too small",[](Image& x){x.bpb([](Sector& b){put32(b,36,1);});});
    refusal("FAT consumes partition",[](Image& x){x.bpb([](Sector& b){put32(b,36,0xffffffff);});});
    refusal("root cluster bounds",[](Image& x){x.bpb([](Sector& b){put32(b,44,0xffffffff);});});
    refusal("hidden-sector mismatch",[](Image& x){x.bpb([](Sector& b){put32(b,28,0);});});
    refusal("total-sector mismatch",[](Image& x){x.bpb([](Sector& b){put32(b,32,100);});});
    refusal("already bootmarked",[](Image& x){x.bpb([](Sector& b){b=marked(b);});});
    refusal("partially bootmarked",[](Image& x){x.bpb([](Sector& b){std::memcpy(b.data()+92,"BOOTDISK",8);});});
    refusal("backup mismatch",[](Image& x){x.blocks[2054][100]^=1;});
    refusal("backup zero",[](Image& x){x.bpb([](Sector& b){put16(b,50,0);});});
    refusal("backup absent",[](Image& x){x.bpb([](Sector& b){put16(b,50,0xffff);});});
    refusal("backup outside reserved",[](Image& x){x.bpb([](Sector& b){put16(b,50,32);});});
    refusal("backup FSInfo outside reserved",[](Image& x){x.bpb([](Sector& b){put16(b,50,31);});});
    refusal("FSInfo collides with boot",[](Image& x){x.bpb([](Sector& b){put16(b,48,0);});});
    refusal("FSInfo collides with backup",[](Image& x){x.bpb([](Sector& b){put16(b,48,6);});});
    refusal("bad FSInfo lead",[](Image& x){x.blocks[2049][0]=0;});
    refusal("bad FSInfo struct",[](Image& x){x.blocks[2049][484]=0;});
    refusal("bad backup FSInfo",[](Image& x){x.blocks[2055][510]=0;});
    refusal("bad FSInfo free count",[](Image& x){put32(x.blocks[2049],488,0x7fffffff);});
    refusal("bad FSInfo next cluster",[](Image& x){put32(x.blocks[2049],492,1);});
    refusal("missing executable",[](Image& x){x.blocks[10272][0]=0;},true);
    refusal("duplicate executable",[](Image& x){std::copy_n(x.blocks[10272].begin(),32,x.blocks[10272].begin()+32);},true);
    refusal("executable is directory",[](Image& x){x.blocks[10272][11]=0x10;},true);
    refusal("executable oversized",[](Image& x){put32(x.blocks[10272],28,9*1024*1024);},true);
    refusal("FAT copies differ",[](Image& x){put32(x.blocks[6176],12,0);},true);
    refusal("bad/free executable chain",[](Image& x){put32(x.blocks[2080],12,0);put32(x.blocks[6176],12,0);},true);
    refusal("truncated executable chain",[](Image& x){put32(x.blocks[10272],28,40000);},true);
    refusal("cyclic executable chain",[](Image& x){put32(x.blocks[10272],28,40000);put32(x.blocks[2080],12,3);put32(x.blocks[6176],12,3);},true);
    refusal("chain outside data area",[](Image& x){put16(x.blocks[10272],26,0);},true);
    Image changed;Layout planned=inspect(changed.reader(),changed.c);changed.blocks[0][440]=0x42;const auto state=changed.blocks;bool rejected=false;try{changed.apply(planned);}catch(...){rejected=true;}check(rejected&&changed.writes==0&&changed.blocks==state,"late prewrite sector change refuses untouched");
    Image failed;planned=inspect(failed.reader(),failed.c);failed.failWrite=2;bool error=false;try{failed.apply(planned);}catch(...){error=true;}check(error&&failed.writes==2,"midwrite failure stops without retry");check(failed.blocks[2048]==planned.primary&&failed.blocks[2054]==marked(planned.secondary),"midwrite failure exposes partial state without blind rollback");
}
void json_tests(){
    const std::string good="{\"canon_firmware\":\"1.1.0\",\"platform\":\"1300D.110\",\"magic_lantern_commit\":\"142472e95054d73640f50789ac0fd66325499d82\",\"artifact_hashes\":{\"AUTOEXEC.BIN\":\""+std::string(64,'a')+"\"},\"metadata\":[true,false,null,1,-2.5e+2]}";
    check(package_autoexec_hash(good)==std::string(64,'a'),"valid package manifest");
    const auto bad=[&](std::string s){bool rejected=false;try{(void)package_autoexec_hash(s);}catch(...){rejected=true;}check(rejected,"malformed/wrong package refused");};
    bad(good+"x");bad(good.substr(0,good.size()-1));bad("{\"x\":1,\"x\":2}");bad("{\"x\":01}");bad("{\"x\":NaN}");bad("{\"x\":\"\\u0000\"}");
    std::string s=good;s.replace(s.find("1.1.0"),5,"1.2.0");bad(s);s=good;s.replace(s.find("1300D.110"),9,"1300D.120");bad(s);s=good;s[s.find(std::string(64,'a'))]='z';bad(s);
    bad(std::string(40,'[')+"0"+std::string(40,']'));bad(std::string(2*1024*1024+1,' '));
}
#ifdef _WIN32
void file_integration(){
    // Exclusive regular sparse image, never a device path. Uses the same planner/writer as production.
    wchar_t temp[MAX_PATH]{},path[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temp)>0,"fixture temp path");check(GetTempFileNameW(temp,L"t6f",0,path)!=0,"fixture temp file");
    HANDLE h=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);check(h!=INVALID_HANDLE_VALUE,"fixture file opened");
    try{DWORD done=0;check(DeviceIoControl(h,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&done,nullptr)!=0,"sparse fixture flag");LARGE_INTEGER end{};end.QuadPart=ExpectedBytes;check(SetFilePointerEx(h,end,nullptr,FILE_BEGIN)&&SetEndOfFile(h),"fixture logical size");
        Image mem;const auto seek=[&](uint64_t n){LARGE_INTEGER pos{};pos.QuadPart=static_cast<LONGLONG>(n*512);check(SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)!=0,"fixture seek");};
        const auto write=[&](uint64_t n,const Sector& block){seek(n);DWORD bytes=0;check(WriteFile(h,block.data(),512,&bytes,nullptr)&&bytes==512,"fixture sector write");};
        const Read read=[&](uint64_t n){seek(n);Sector block{};DWORD bytes=0;check(ReadFile(h,block.data(),512,&bytes,nullptr)&&bytes==512,"fixture sector read");return block;};
        for(const auto& pair:mem.blocks)write(pair.first,pair.second);const Layout l=inspect(read,mem.c);check(read_autoexec(read,l).size()==900,"file-backed raw AUTOEXEC read");
        apply_markers(l,read,write,[&]{check(FlushFileBuffers(h)!=0,"fixture flush");});
        for(const auto& pair:mem.blocks)check(read(pair.first)==((pair.first==2048||pair.first==2054)?marked(pair.second):pair.second),"file fixture exact delta");check(read(30000)==Sector{},"unallocated data stays zero");
    }catch(...){CloseHandle(h);throw;}CloseHandle(h);
}
#endif
}
int main(){try{layout_tests();json_tests();
#ifdef _WIN32
file_integration();
#endif
std::cout<<"PASS "<<checks<<" checks; synthetic sparse images only; no hardware accessed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
