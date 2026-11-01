#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <objbase.h>
#include <memory>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "card_layout.hpp"
#include "package_json.hpp"

using namespace t6card;
namespace {
constexpr const wchar_t* Volume=L"\\\\.\\H:";
constexpr const wchar_t* Root=L"H:\\";
class Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
public:
    Handle()=default;explicit Handle(HANDLE v):h(v){}
    ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& o) noexcept:h(o.h){o.h=INVALID_HANDLE_VALUE;}
    Handle& operator=(Handle&& o) noexcept{if(this!=&o){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=o.h;o.h=INVALID_HANDLE_VALUE;}return *this;}
    HANDLE get()const{return h;}
};
[[noreturn]] void winfail(const char* message){throw std::runtime_error(std::string(message)+" (Win32 "+std::to_string(GetLastError())+")");}
void ok(BOOL value,const char* message){if(!value)winfail(message);}
Handle open(const std::wstring& path,DWORD access,DWORD flags=0){
    Handle h(CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,flags,nullptr));
    if(h.get()==INVALID_HANDLE_VALUE)winfail("open failed; no elevation or permission fallback attempted");return h;
}
std::string utf8(const std::wstring& w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);require(n>0,"invalid Windows string");std::string s(static_cast<size_t>(n),0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;}
std::string hash(const void* bytes,size_t size){
    require(size<=MAXDWORD,"hash input too large");BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE state=nullptr;
    require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"open SHA256 provider failed");
    std::array<uint8_t,32> out{};
    const NTSTATUS a=BCryptCreateHash(alg,&state,nullptr,0,nullptr,0,0);
    const NTSTATUS b=a>=0?BCryptHashData(state,const_cast<PUCHAR>(static_cast<const UCHAR*>(bytes)),static_cast<ULONG>(size),0):a;
    const NTSTATUS c=b>=0?BCryptFinishHash(state,out.data(),static_cast<ULONG>(out.size()),0):b;
    if(state)BCryptDestroyHash(state);BCryptCloseAlgorithmProvider(alg,0);require(c>=0,"SHA256 failed");
    std::ostringstream s;for(uint8_t x:out)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(x);return s.str();
}
template<class T>std::string hash(const T& v){return hash(v.data(),v.size());}
void ioctl(HANDLE h,DWORD code,void* in,DWORD inSize,void* out,DWORD outSize,DWORD* actual=nullptr){DWORD n=0;ok(DeviceIoControl(h,code,in,inSize,out,outSize,&n,nullptr),"device query/control failed");if(actual)*actual=n;}
template<class T>T query(HANDLE h,DWORD code){T out{};DWORD n=0;ioctl(h,code,nullptr,0,&out,sizeof(out),&n);require(n>=sizeof(T),"truncated device query");return out;}
DISK_EXTENT extent(HANDLE h){alignas(8) std::array<uint8_t,4096> b{};DWORD n=0;ioctl(h,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,nullptr,0,b.data(),static_cast<DWORD>(b.size()),&n);require(n>=sizeof(VOLUME_DISK_EXTENTS),"truncated volume extents");const auto* x=reinterpret_cast<const VOLUME_DISK_EXTENTS*>(b.data());require(x->NumberOfDiskExtents==1,"volume spans multiple disk extents");return x->Extents[0];}
Sector read_sector(HANDLE h,uint64_t relative){
    require(relative<=uint64_t(INT64_MAX)/512,"sector offset overflow");void* raw=_aligned_malloc(512,4096);require(raw!=nullptr,"aligned buffer allocation failed");
    std::unique_ptr<void,decltype(&_aligned_free)> b(raw,&_aligned_free);LARGE_INTEGER pos{};pos.QuadPart=static_cast<LONGLONG>(relative*512);
    ok(SetFilePointerEx(h,pos,nullptr,FILE_BEGIN),"sector seek failed");DWORD n=0;ok(ReadFile(h,raw,512,&n,nullptr),"sector read failed");require(n==512,"short sector read");Sector s{};std::memcpy(s.data(),raw,512);return s;
}
void write_sector(HANDLE h,uint64_t relative,const Sector& s){
    require(relative<=uint64_t(INT64_MAX)/512,"sector offset overflow");void* raw=_aligned_malloc(512,4096);require(raw!=nullptr,"aligned write buffer allocation failed");
    std::unique_ptr<void,decltype(&_aligned_free)> b(raw,&_aligned_free);std::memcpy(raw,s.data(),512);LARGE_INTEGER pos{};pos.QuadPart=static_cast<LONGLONG>(relative*512);
    ok(SetFilePointerEx(h,pos,nullptr,FILE_BEGIN),"sector write seek failed");DWORD n=0;ok(WriteFile(h,raw,512,&n,nullptr),"sector write failed");require(n==512,"short sector write");
}
DWORD windows_disk(){wchar_t win[MAX_PATH]{},mount[MAX_PATH]{};require(GetWindowsDirectoryW(win,MAX_PATH)>0,"cannot locate Windows system directory");ok(GetVolumePathNameW(win,mount,MAX_PATH),"cannot locate system volume");require(wcslen(mount)==3 && mount[1]==L':',"unsupported Windows system volume path");std::wstring device=L"\\\\.\\";device+=mount[0];device+=L':';auto h=open(device,0);return extent(h.get()).DiskNumber;}
struct Device {
    Handle volume,disk;DISK_EXTENT ex{};Context ctx;std::vector<uint8_t> descriptor;std::wstring guid;DWORD diskNumber=0;
    explicit Device(bool writing){
        require(GetDriveTypeW(Root)==DRIVE_REMOVABLE,"H: is not a removable volume");
        wchar_t label[128]{},fs[32]{},name[128]{};DWORD serial=0,volumeFlags=0;
        ok(GetVolumeInformationW(Root,label,128,&serial,nullptr,&volumeFlags,fs,32),"cannot read H: volume information");
        require(wcscmp(label,L"EOS_DIGITAL")==0 && wcscmp(fs,L"FAT32")==0,"H: must be EOS_DIGITAL / FAT32");
        require((volumeFlags&FILE_READ_ONLY_VOLUME)==0,"H: filesystem is readonly");
        ok(GetVolumeNameForVolumeMountPointW(Root,name,128),"cannot identify H: volume GUID");guid=name;
        volume=open(Volume,GENERIC_READ|(writing?GENERIC_WRITE:0),FILE_FLAG_NO_BUFFERING|(writing?FILE_FLAG_WRITE_THROUGH:0));
        ex=extent(volume.get());diskNumber=ex.DiskNumber;
        disk=open(L"\\\\.\\PhysicalDrive"+std::to_wstring(diskNumber),GENERIC_READ,FILE_FLAG_NO_BUFFERING);
        refresh();ctx.volumeLabel=utf8(label);
    }
    void refresh(){
        const DISK_EXTENT now=extent(volume.get());require(now.DiskNumber==diskNumber && now.StartingOffset.QuadPart==ex.StartingOffset.QuadPart && now.ExtentLength.QuadPart==ex.ExtentLength.QuadPart,"volume extent identity changed");
        alignas(8) std::array<uint8_t,4096> geom{};DWORD n=0;ioctl(disk.get(),IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,nullptr,0,geom.data(),static_cast<DWORD>(geom.size()),&n);require(n>=sizeof(DISK_GEOMETRY_EX),"truncated geometry");
        const auto* g=reinterpret_cast<const DISK_GEOMETRY_EX*>(geom.data());require(g->DiskSize.QuadPart>0 && ex.StartingOffset.QuadPart>0 && ex.ExtentLength.QuadPart>0,"negative disk geometry");
        ctx.diskBytes=static_cast<uint64_t>(g->DiskSize.QuadPart);ctx.bytesPerSector=g->Geometry.BytesPerSector;
        ctx.extentStart=static_cast<uint64_t>(ex.StartingOffset.QuadPart);ctx.extentBytes=static_cast<uint64_t>(ex.ExtentLength.QuadPart);ctx.oneExtent=true;
        STORAGE_PROPERTY_QUERY q{};q.PropertyId=StorageDeviceProperty;q.QueryType=PropertyStandardQuery;
        descriptor.assign(16384,0);ioctl(disk.get(),IOCTL_STORAGE_QUERY_PROPERTY,&q,sizeof(q),descriptor.data(),static_cast<DWORD>(descriptor.size()),&n);
        require(n>=sizeof(STORAGE_DEVICE_DESCRIPTOR),"truncated storage descriptor");const auto* d=reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptor.data());require(d->Size>=sizeof(STORAGE_DEVICE_DESCRIPTOR)&&d->Size<=n,"invalid storage descriptor size");descriptor.resize(d->Size);
        ctx.removable=d->RemovableMedia!=FALSE && d->DeviceType==0;ctx.allowedBus=d->BusType==BusTypeUsb || d->BusType==BusTypeSd || d->BusType==BusTypeMmc;
        const auto attr=query<GET_DISK_ATTRIBUTES>(disk.get(),IOCTL_DISK_GET_DISK_ATTRIBUTES);ctx.online=(attr.Attributes&DISK_ATTRIBUTE_OFFLINE)==0;ctx.writable=(attr.Attributes&DISK_ATTRIBUTE_READ_ONLY)==0;
        if(!DeviceIoControl(disk.get(),IOCTL_DISK_IS_WRITABLE,nullptr,0,nullptr,0,&n,nullptr)){require(GetLastError()==ERROR_WRITE_PROTECT,"cannot establish disk writable status");ctx.writable=false;}
        alignas(8) std::array<uint8_t,32768> layout{};ioctl(disk.get(),IOCTL_DISK_GET_DRIVE_LAYOUT_EX,nullptr,0,layout.data(),static_cast<DWORD>(layout.size()),&n);
        require(n>=offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry),"truncated drive layout");const auto* l=reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(layout.data());
        require(l->PartitionCount<=128 && n>=offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry)+size_t(l->PartitionCount)*sizeof(PARTITION_INFORMATION_EX),"drive partition list out of bounds");
        ctx.mbrStyle=l->PartitionStyle==PARTITION_STYLE_MBR;unsigned count=0;
        for(DWORD i=0;i<l->PartitionCount;i++)if(l->PartitionEntry[i].PartitionLength.QuadPart>0){++count;const auto& p=l->PartitionEntry[i];require(!p.IsServicePartition && p.StartingOffset.QuadPart==ex.StartingOffset.QuadPart && p.PartitionLength.QuadPart==ex.ExtentLength.QuadPart,"partition layout is not the sole H: extent");}
        ctx.onePartition=count==1;ctx.systemDisk=windows_disk()==diskNumber;
    }
    Sector read(uint64_t absolute){if(absolute==0)return read_sector(disk.get(),0);require(absolute>=ctx.extentStart/512 && absolute-ctx.extentStart/512<ctx.extentBytes/512,"read outside H: partition");return read_sector(volume.get(),absolute-ctx.extentStart/512);}
    Read reader(){return[this](uint64_t sector){return read(sector);};}
    std::string identity(const Layout& l){
        std::ostringstream text;text<<diskNumber<<":"<<ctx.diskBytes<<":"<<ctx.extentStart<<":"<<ctx.extentBytes<<":"<<utf8(guid)<<":"<<hash(descriptor)<<":"<<hash(l.mbr)<<":"<<hash(l.primary)<<":"<<hash(l.secondary)<<":"<<hash(l.info)<<":"<<hash(l.backupInfo);
        const std::string s=text.str();return hash(s);
    }
};
std::vector<uint8_t> read_file(const std::wstring& path,size_t cap,bool forbidH){
    Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(h.get()==INVALID_HANDLE_VALUE)winfail("cannot open required package/card file");
    BY_HANDLE_FILE_INFORMATION fi{};ok(GetFileInformationByHandle(h.get(),&fi),"file metadata failed");require((fi.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==0,"expected a regular file");
    if(forbidH){wchar_t finalName[32768]{},mount[32768]{},guid[128]{},hguid[128]{};require(GetFinalPathNameByHandleW(h.get(),finalName,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS)>0,"cannot resolve package source");ok(GetVolumePathNameW(finalName,mount,32768),"cannot resolve package source volume");ok(GetVolumeNameForVolumeMountPointW(mount,guid,128),"cannot identify package source volume");ok(GetVolumeNameForVolumeMountPointW(Root,hguid,128),"cannot identify card volume");require(_wcsicmp(guid,hguid)!=0,"package source must be outside H:");}
    LARGE_INTEGER len{};ok(GetFileSizeEx(h.get(),&len),"file size failed");require(len.QuadPart>0 && static_cast<uint64_t>(len.QuadPart)<=cap,"required file is empty or too large");std::vector<uint8_t> out(static_cast<size_t>(len.QuadPart));DWORD n=0;ok(ReadFile(h.get(),out.data(),static_cast<DWORD>(out.size()),&n,nullptr),"file read failed");require(n==out.size(),"short file read");return out;
}
struct Package {std::string manifestHash,autoexecHash;size_t autoexecSize=0;};
Package validate_package(const std::wstring& dir,const std::string& pin){
    require(hex_hash(pin),"manifest SHA256 pin must be 64 lowercase hex characters");
    const auto manifest=read_file(dir+L"\\manifest.json",2*1024*1024,true);require(hash(manifest)==pin,"source manifest does not match its SHA256 pin");
    const std::string text(manifest.begin(),manifest.end());const std::string expected=package_autoexec_hash(text);
    const auto exe=read_file(dir+L"\\AUTOEXEC.BIN",8*1024*1024,true);require(hash(exe)==expected,"source AUTOEXEC does not match manifest");
    const auto cardManifest=read_file(L"H:\\manifest.json",2*1024*1024,false);require(cardManifest==manifest,"card manifest is absent or differs from source package");
    return{pin,expected,exe.size()};
}
void create_private_dir(const std::wstring& path,SECURITY_ATTRIBUTES* sa){
    if(!CreateDirectoryW(path.c_str(),sa)){require(GetLastError()==ERROR_ALREADY_EXISTS,"cannot create private backup directory");const DWORD a=GetFileAttributesW(path.c_str());require(a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY) && !(a&FILE_ATTRIBUTE_REPARSE_POINT),"backup directory is a reparse point or invalid");}
}
std::wstring backup(const Device& d,const Layout& l,const std::string& token,const Package& pkg){
    PWSTR local=nullptr;require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local)),"cannot locate private local app data");std::wstring base=local;CoTaskMemFree(local);
    require(base.size()>3 && base[1]==L':' && towupper(base[0])!=L'H',"backup location must be a local drive outside H:");
    {auto h=open(base,FILE_READ_ATTRIBUTES,FILE_FLAG_BACKUP_SEMANTICS);wchar_t finalName[32768]{},mount[32768]{},guid[128]{},cardGuid[128]{};
        const DWORD count=GetFinalPathNameByHandleW(h.get(),finalName,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);require(count>0&&count<32768,"cannot resolve backup parent");
        ok(GetVolumePathNameW(finalName,mount,32768),"cannot locate backup parent volume");ok(GetVolumeNameForVolumeMountPointW(mount,guid,128),"cannot identify backup parent volume");ok(GetVolumeNameForVolumeMountPointW(Root,cardGuid,128),"cannot identify card volume");require(_wcsicmp(guid,cardGuid)!=0,"backup parent resolves to H: card");}
    Handle tok;HANDLE t=nullptr;ok(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t),"cannot read current user SID");tok=Handle(t);DWORD needed=0;GetTokenInformation(tok.get(),TokenUser,nullptr,0,&needed);require(needed>0&&needed<65536,"invalid token size");std::vector<uint8_t> user(needed);ok(GetTokenInformation(tok.get(),TokenUser,user.data(),needed,&needed),"cannot read user token");LPWSTR sid=nullptr;ok(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid),"cannot encode user SID");std::wstring sddl=L"D:P(A;;FA;;;SY)(A;;FA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR sd=nullptr;ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr),"cannot create private backup ACL");std::unique_ptr<void,decltype(&LocalFree)> release(sd,&LocalFree);SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    base+=L"\\T6Cinema";create_private_dir(base,&sa);base+=L"\\card-backups";create_private_dir(base,&sa);GUID id{};require(SUCCEEDED(CoCreateGuid(&id)),"cannot create backup identity");wchar_t idstr[64]{};StringFromGUID2(id,idstr,64);base+=L"\\"+std::wstring(idstr);ok(CreateDirectoryW(base.c_str(),&sa),"cannot create exclusive backup folder");
    const auto save=[&](const wchar_t* name,const void* bytes,size_t size){const std::wstring path=base+L"\\"+name;Handle f(CreateFileW(path.c_str(),GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr));if(f.get()==INVALID_HANDLE_VALUE)winfail("cannot create exclusive backup file");DWORD n=0;ok(WriteFile(f.get(),bytes,static_cast<DWORD>(size),&n,nullptr),"backup write failed");require(n==size,"short backup write");ok(FlushFileBuffers(f.get()),"backup flush failed");};
    save(L"mbr.bin",l.mbr.data(),512);save(L"primary-boot.bin",l.primary.data(),512);save(L"backup-boot.bin",l.secondary.data(),512);save(L"fsinfo.bin",l.info.data(),512);save(L"backup-fsinfo.bin",l.backupInfo.data(),512);
    std::ostringstream meta;meta<<"{\n  \"volume\": \"H:\",\n  \"identity\": \""<<token<<"\",\n  \"disk_bytes\": "<<d.ctx.diskBytes<<",\n  \"partition_start_sector\": "<<l.start<<",\n  \"backup_relative_sector\": "<<l.backup<<",\n  \"manifest_sha256\": \""<<pkg.manifestHash<<"\",\n  \"autoexec_sha256\": \""<<pkg.autoexecHash<<"\"\n}\n";const std::string m=meta.str();save(L"original.json",m.data(),m.size());
    require(read_file(base+L"\\mbr.bin",512,true)==std::vector<uint8_t>(l.mbr.begin(),l.mbr.end()) && read_file(base+L"\\primary-boot.bin",512,true)==std::vector<uint8_t>(l.primary.begin(),l.primary.end()) && read_file(base+L"\\backup-boot.bin",512,true)==std::vector<uint8_t>(l.secondary.begin(),l.secondary.end()) && read_file(base+L"\\fsinfo.bin",512,true)==std::vector<uint8_t>(l.info.begin(),l.info.end()) && read_file(base+L"\\backup-fsinfo.bin",512,true)==std::vector<uint8_t>(l.backupInfo.begin(),l.backupInfo.end()),"private backup readback failed");return base;
}
std::string narrow_hash(const std::wstring& s){const std::string out=utf8(s);require(hex_hash(out),"identity/hash argument is invalid");return out;}
}
int wmain(int argc,wchar_t** argv){
    bool writeStarted=false;std::wstring backupPath;
    try{
        bool mark=false;std::string expected,pin;std::wstring package;
        if(argc==2 && (wcscmp(argv[1],L"--help")==0 || wcscmp(argv[1],L"-h")==0)){
            std::cout<<"T6 local H: card utility. No format, camera, driver or elevation operations.\nDefault: --inspect (read only).\nMark: --mark --identity <inspection-token> --package <source-directory> --manifest-sha256 <sha256>\nOnly the reviewed 15931539456-byte removable FAT32 H: card is supported.\n";return 0;}
        for(int i=1;i<argc;i++){
            const std::wstring a=argv[i];
            if(a==L"--inspect"){require(argc==2,"--inspect takes no other arguments");}
            else if(a==L"--mark"){require(!mark,"duplicate --mark");mark=true;}
            else if(a==L"--identity"||a==L"--package"||a==L"--manifest-sha256"){
                require(i+1<argc,"missing option value");const std::wstring v=argv[++i];
                if(a==L"--identity"){require(expected.empty(),"duplicate identity");expected=narrow_hash(v);}else if(a==L"--package"){require(package.empty(),"duplicate package");package=v;}else{require(pin.empty(),"duplicate manifest pin");pin=narrow_hash(v);}
            }else require(false,"unknown option; arbitrary device selection is not supported");
        }
        require(mark || (expected.empty()&&pin.empty()&&package.empty()),"mark options require --mark");
        require(!mark || (!expected.empty()&&!pin.empty()&&!package.empty()),"--mark requires identity, package and manifest SHA256");
        Device d(mark);Layout l=inspect(d.reader(),d.ctx);const std::string token=d.identity(l);
        if(!mark){std::cout<<"{\"mode\":\"inspect\",\"read_only\":true,\"volume\":\"H:\",\"disk_number\":"<<d.diskNumber<<",\"disk_bytes\":"<<d.ctx.diskBytes<<",\"extent_start\":"<<d.ctx.extentStart<<",\"extent_bytes\":"<<d.ctx.extentBytes<<",\"partition_start_sector\":"<<l.start<<",\"backup_relative_sector\":"<<l.backup<<",\"fsinfo_relative_sector\":"<<l.fsinfo<<",\"identity\":\""<<token<<"\"}\n";return 0;}
        require(token==expected,"inspection identity does not match current H: card");const Package pkg=validate_package(package,pin);
        DWORD n=0;ok(DeviceIoControl(d.volume.get(),FSCTL_LOCK_VOLUME,nullptr,0,nullptr,0,&n,nullptr),"exclusive volume lock failed; no write attempted");
        d.refresh();l=inspect(d.reader(),d.ctx);require(d.identity(l)==expected,"card identity changed before exclusive lock");
        const auto cardExe=read_autoexec(d.reader(),l);require(cardExe.size()==pkg.autoexecSize && hash(cardExe)==pkg.autoexecHash,"locked raw AUTOEXEC.BIN does not match reviewed source package");
        backupPath=backup(d,l,token,pkg);
        apply_markers(l,d.reader(),[&](uint64_t sector,const Sector& block){require(sector==l.start || sector==uint64_t(l.start)+l.backup,"write outside the two planned boot sectors refused");writeStarted=true;write_sector(d.volume.get(),sector-l.start,block);},[&]{ok(FlushFileBuffers(d.volume.get()),"volume flush failed");});
        std::wcout<<L"Marked and readback verified. Private original-sector backup: "<<backupPath<<L"\n";
        return 0; // RAII closes the exclusive lock; no filesystem/card format or dismount.
    }catch(const std::exception& e){
        std::cerr<<"REFUSED/FAILED: "<<e.what()<<"\n";
        if(writeStarted)std::cerr<<"A WRITE WAS ATTEMPTED: partial card state is possible. No automatic retry/rollback was attempted. Retain private backups and inspect before any further operation.\n";
        else std::cerr<<"No card-sector write was attempted.\n";
        if(!backupPath.empty())std::wcerr<<L"Private backup: "<<backupPath<<L"\n";
        return writeStarted?2:1;
    }
}
