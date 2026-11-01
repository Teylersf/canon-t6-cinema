#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace t6card {
constexpr uint64_t ExpectedBytes = 15931539456ULL;
using Sector = std::array<uint8_t, 512>;
using Read = std::function<Sector(uint64_t)>; // absolute logical sector
inline void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
inline uint16_t u16(const Sector& s, size_t p) { return uint16_t(s[p] | (uint16_t(s[p+1]) << 8)); }
inline uint32_t u32(const Sector& s, size_t p) { return uint32_t(u16(s,p)) | (uint32_t(u16(s,p+2)) << 16); }
inline void signature(const Sector& s) { require(s[510]==0x55 && s[511]==0xaa, "missing sector signature"); }

struct Context {
    uint64_t diskBytes = 0, extentStart = 0, extentBytes = 0;
    uint32_t bytesPerSector = 0;
    bool removable = false, allowedBus = false, writable = false;
    bool systemDisk = true, oneExtent = false, onePartition = false;
    bool mbrStyle = false, online = false;
    std::string volumeLabel;
};
struct Layout {
    uint32_t start = 0, sectors = 0, fatSectors = 0, clusters = 0, rootCluster = 0;
    uint16_t reserved = 0, backup = 0, fsinfo = 0;
    uint8_t spc = 0, fats = 0;
    Sector mbr{}, primary{}, secondary{}, info{}, backupInfo{};
};

inline void check_context(const Context& c) {
    require(c.diskBytes == ExpectedBytes, "physical capacity is not the reviewed 15,931,539,456-byte card");
    require(c.bytesPerSector == 512, "only 512-byte logical sectors are supported");
    require(c.removable && c.allowedBus, "card must be removable USB/SD/MMC media");
    require(c.writable && c.online && !c.systemDisk, "readonly/offline/system disk refused");
    require(c.oneExtent && c.onePartition && c.mbrStyle, "requires one MBR partition and one volume extent");
    require(c.volumeLabel == "EOS_DIGITAL", "volume label must be EOS_DIGITAL");
    require(c.extentStart >= 512 && c.extentStart % 512 == 0 && c.extentBytes % 512 == 0,
            "unaligned or empty volume extent");
    require(c.extentBytes > 0 && c.extentStart <= c.diskBytes && c.extentBytes <= c.diskBytes-c.extentStart,
            "volume extent outside disk");
}
inline void check_fsinfo(const Sector& s, uint32_t clusters) {
    require(u32(s,0)==0x41615252 && u32(s,484)==0x61417272 && u32(s,508)==0xaa550000,
            "invalid FAT32 FSInfo signatures");
    const uint32_t free = u32(s,488), next = u32(s,492);
    require(free==0xffffffff || free<=clusters, "invalid FSInfo free-cluster count");
    require(next==0xffffffff || (next>=2 && next<=clusters+1), "invalid FSInfo next cluster");
}
inline Layout inspect(const Read& read, const Context& c) {
    check_context(c);
    Layout l;
    l.mbr=read(0); signature(l.mbr);
    unsigned count=0; size_t p=0;
    for (size_t i=0;i<4;i++) {
        const size_t at=446+i*16;
        const bool nonzero=std::any_of(l.mbr.begin()+at,l.mbr.begin()+at+16,[](uint8_t x){return x!=0;});
        if (nonzero) { ++count; p=at; }
    }
    require(count==1, "MBR must have exactly one nonempty partition entry");
    require(l.mbr[p]==0 || l.mbr[p]==0x80, "invalid MBR boot indicator");
    require(l.mbr[p+4]==0x0b || l.mbr[p+4]==0x0c, "partition is not FAT32 MBR type 0B/0C");
    l.start=u32(l.mbr,p+8); l.sectors=u32(l.mbr,p+12);
    require(uint64_t(l.start)*512==c.extentStart && uint64_t(l.sectors)*512==c.extentBytes,
            "MBR and Windows volume extents disagree");
    l.primary=read(l.start); signature(l.primary);
    const auto& b=l.primary;
    require((b[0]==0xeb && b[2]==0x90) || b[0]==0xe9, "invalid FAT jump instruction");
    require(u16(b,11)==512, "FAT BPB bytes-per-sector is not 512");
    l.spc=b[13]; l.reserved=u16(b,14); l.fats=b[16]; l.fatSectors=u32(b,36);
    require(l.spc && l.spc<=128 && (l.spc&(l.spc-1))==0, "invalid sectors-per-cluster");
    require(l.reserved>=8 && l.reserved<=4096 && l.fats==2 && l.fatSectors>0, "invalid FAT32 reserved/FAT geometry");
    require(u16(b,17)==0 && u16(b,19)==0 && u16(b,22)==0 && u16(b,42)==0 && u16(b,40)==0,
            "unsupported FAT32 BPB variant or disabled mirroring");
    require(u32(b,28)==l.start && u32(b,32)==l.sectors, "FAT hidden/total sectors disagree with partition");
    require(b[21]==0xf8 && b[66]==0x29 && std::memcmp(b.data()+82,"FAT32   ",8)==0, "invalid FAT32 extended BPB");
    require(std::memcmp(b.data()+71,"EOS_DIGITAL",11)==0, "BPB label is not unmarked EOS_DIGITAL");
    require(std::memcmp(b.data()+92,"BOOTDISK",8)!=0, "card is already or partially bootmarked");
    const uint64_t overhead=uint64_t(l.reserved)+uint64_t(l.fats)*l.fatSectors;
    require(overhead<l.sectors, "FAT area consumes partition");
    const uint64_t clusterCount=(l.sectors-overhead)/l.spc;
    require(clusterCount>=65525 && clusterCount<0x0ffffff5, "cluster count is not FAT32");
    l.clusters=static_cast<uint32_t>(clusterCount);
    require(uint64_t(l.fatSectors)*512>=uint64_t(l.clusters+2)*4, "FAT table too small for data clusters");
    l.rootCluster=u32(b,44);
    require(l.rootCluster>=2 && l.rootCluster<=l.clusters+1, "invalid root cluster");
    l.fsinfo=u16(b,48); l.backup=u16(b,50);
    require(l.fsinfo>0 && l.backup>0 && l.backup!=0xffff && l.fsinfo!=l.backup,
            "invalid FAT32 FSInfo/backup offsets");
    require(uint32_t(l.backup)+l.fsinfo<l.reserved && l.fsinfo<l.reserved && l.backup<l.reserved,
            "backup boot/FSInfo sector outside reserved area");
    l.secondary=read(uint64_t(l.start)+l.backup); signature(l.secondary);
    require(l.primary==l.secondary, "primary and backup boot sectors differ before marking");
    l.info=read(uint64_t(l.start)+l.fsinfo);
    l.backupInfo=read(uint64_t(l.start)+l.backup+l.fsinfo);
    check_fsinfo(l.info,l.clusters); check_fsinfo(l.backupInfo,l.clusters);
    return l;
}
inline uint32_t next_cluster(const Read& read,const Layout& l,uint32_t cluster) {
    require(cluster>=2 && cluster<=l.clusters+1,"cluster outside FAT32 data area");
    const uint32_t sector=cluster/128, offset=(cluster%128)*4;
    require(sector<l.fatSectors,"FAT lookup outside table");
    const Sector a=read(uint64_t(l.start)+l.reserved+sector);
    const Sector b=read(uint64_t(l.start)+l.reserved+l.fatSectors+sector);
    const uint32_t va=u32(a,offset)&0x0fffffff, vb=u32(b,offset)&0x0fffffff;
    require(va==vb,"FAT mirrors disagree");
    require(va>=0x0ffffff8 || (va>=2 && va<=l.clusters+1),"invalid/bad/free FAT chain entry");
    return va;
}
inline uint64_t cluster_sector(const Layout& l,uint32_t c) {
    require(c>=2 && c<=l.clusters+1,"cluster out of bounds");
    return uint64_t(l.start)+l.reserved+uint64_t(l.fats)*l.fatSectors+uint64_t(c-2)*l.spc;
}
// Read AUTOEXEC.BIN directly after locking the volume; no filesystem handle or TOCTOU gap.
inline std::vector<uint8_t> read_autoexec(const Read& read,const Layout& l) {
    uint32_t dir=l.rootCluster, first=0, size=0; bool found=false, end=false;
    std::set<uint32_t> seen;
    while(!end) {
        require(seen.insert(dir).second && seen.size()<=128,"root directory chain loops or exceeds limit");
        for(uint32_t s=0;s<l.spc && !end;s++) {
            const Sector block=read(cluster_sector(l,dir)+s);
            for(size_t i=0;i<512;i+=32) {
                if(block[i]==0) {end=true;break;}
                if(block[i]==0xe5 || block[i+11]==0x0f) continue;
                if(std::memcmp(block.data()+i,"AUTOEXECBIN",11)==0) {
                    require(!found && (block[i+11]&0x18)==0,"duplicate/non-file AUTOEXEC.BIN");
                    found=true; first=(uint32_t(u16(block,i+20))<<16)|u16(block,i+26); size=u32(block,i+28);
                }
            }
        }
        const uint32_t n=next_cluster(read,l,dir);
        if(end || n>=0x0ffffff8) break;
        dir=n;
    }
    require(found && size>0 && size<=8*1024*1024,"missing or oversized AUTOEXEC.BIN");
    std::vector<uint8_t> out;out.reserve(size);seen.clear();uint32_t at=first;
    while(out.size()<size) {
        require(seen.insert(at).second,"AUTOEXEC cluster chain loops");
        for(uint32_t s=0;s<l.spc && out.size()<size;s++) {
            const Sector block=read(cluster_sector(l,at)+s);
            const size_t n=std::min<size_t>(512,size-out.size());out.insert(out.end(),block.begin(),block.begin()+n);
        }
        const uint32_t n=next_cluster(read,l,at);
        if(out.size()<size) {require(n<0x0ffffff8,"truncated AUTOEXEC chain");at=n;}
        else require(n>=0x0ffffff8,"overallocated AUTOEXEC chain refused");
    }
    return out;
}
inline Sector marked(const Sector& original) {
    Sector out=original;
    // ML 142472e95054d73640f50789ac0fd66325499d82 src/bootflags.c:167-170.
    std::memcpy(out.data()+0x47,"EOS_DEVELOP",11);
    std::memcpy(out.data()+0x5c,"BOOTDISK",8);
    return out;
}
inline void verify_marked(const Layout& before,const Sector& primary,const Sector& backup) {
    require(primary==marked(before.primary) && backup==marked(before.secondary),
            "boot-sector readback differs from the exact planned marker-only changes");
}
inline void apply_markers(const Layout& l,const Read& read,
                          const std::function<void(uint64_t,const Sector&)>& write,
                          const std::function<void()>& flush) {
    require(read(0)==l.mbr && read(l.start)==l.primary && read(uint64_t(l.start)+l.backup)==l.secondary &&
            read(uint64_t(l.start)+l.fsinfo)==l.info && read(uint64_t(l.start)+l.backup+l.fsinfo)==l.backupInfo,
            "disk sectors changed after validation; no write attempted");
    // No retry or automatic rollback: a real I/O failure/power loss is not atomic.
    write(uint64_t(l.start)+l.backup,marked(l.secondary));flush();
    require(read(uint64_t(l.start)+l.backup)==marked(l.secondary),"backup boot-sector write verification failed");
    write(l.start,marked(l.primary));flush();
    verify_marked(l,read(l.start),read(uint64_t(l.start)+l.backup));
    require(read(0)==l.mbr && read(uint64_t(l.start)+l.fsinfo)==l.info &&
            read(uint64_t(l.start)+l.backup+l.fsinfo)==l.backupInfo,"MBR/FSInfo changed during marking");
}
} // namespace t6card
