#include "executable_compatibility.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace eu4menu {
namespace {
bool memory_access(const void* image,std::size_t offset,std::size_t size,ImageAccess access=ImageAccess::read) {
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const auto maximum=(std::numeric_limits<std::uintptr_t>::max)();
    if(!base||!size||offset>maximum-base||size>maximum-base-offset) return false;
    auto current=base+offset;const auto end=current+size;
    while(current<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void*>(current),&region,sizeof(region))||
           region.AllocationBase!=image||region.State!=MEM_COMMIT||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const auto protection=region.Protect&0xff;
        const bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||
            protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READ||
            protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        const bool executable=protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||
            protection==PAGE_EXECUTE_WRITECOPY;
        const bool writable=protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        if(!readable||(access==ImageAccess::code&&!executable)||(access==ImageAccess::write&&!writable)) return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>maximum-begin||begin+region.RegionSize<=current) return false;
        current=(std::min)(end,begin+region.RegionSize);
    }
    return true;
}
template<class T> bool read(const void* image,std::size_t offset,T& value) {
    if(!memory_access(image,offset,sizeof(value))) return false;
    std::memcpy(&value,static_cast<const std::byte*>(image)+offset,sizeof(value));return true;
}
bool section_access(DWORD flags,ImageAccess access) {
    if(!(flags&IMAGE_SCN_MEM_READ)) return false;
    return access==ImageAccess::code?(flags&IMAGE_SCN_MEM_EXECUTE)!=0:
        access==ImageAccess::write?(flags&IMAGE_SCN_MEM_WRITE)!=0:true;
}
std::string position(const char* name,std::size_t rva,const char* reason) {
    std::ostringstream out;out<<"Refused: "<<name<<" at RVA 0x"<<std::hex<<rva<<": "<<reason<<'.';return out.str();
}
int hex_digit(char value) {
    if(value>='0'&&value<='9') return value-'0';
    if(value>='a'&&value<='f') return value-'a'+10;
    if(value>='A'&&value<='F') return value-'A'+10;
    return -1;
}
struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    std::vector<UCHAR> object;
    ~HashHandles() { if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
};
}
ImageCheck check_executable_image(const void* image,const ImageProfile& profile) {
    ImageCheck result;
    auto fail=[&](std::string error){result.error=std::move(error);return result;};
    IMAGE_DOS_HEADER dos{};
    if(!read(image,0,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<static_cast<LONG>(sizeof(dos))||dos.e_lfanew>0x100000)
        return fail("Refused: unsupported executable layout: invalid or unreadable DOS header.");
    const auto nt=static_cast<std::size_t>(dos.e_lfanew);
    DWORD signature{};IMAGE_FILE_HEADER file{};IMAGE_OPTIONAL_HEADER64 optional{};
    if(!read(image,nt,signature)||signature!=IMAGE_NT_SIGNATURE||!read(image,nt+4,file)||
       file.Machine!=IMAGE_FILE_MACHINE_AMD64||!(file.Characteristics&IMAGE_FILE_EXECUTABLE_IMAGE)||
       (file.Characteristics&IMAGE_FILE_DLL)||file.SizeOfOptionalHeader<sizeof(optional)||
       !read(image,nt+4+sizeof(file),optional)||optional.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return fail("Refused: unsupported executable layout: Windows x64 PE32+ executable required.");
    if(!file.NumberOfSections||file.NumberOfSections>96||!optional.SizeOfImage||
       optional.SizeOfHeaders>optional.SizeOfImage||!optional.SectionAlignment)
        return fail("Refused: unsupported executable layout: invalid image or section count.");
    const auto section_table=nt+4+sizeof(file)+file.SizeOfOptionalHeader;
    const auto table_size=static_cast<std::size_t>(file.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER);
    if(section_table>optional.SizeOfHeaders||table_size>optional.SizeOfHeaders-section_table||
       !memory_access(image,section_table,table_size))
        return fail("Refused: unsupported executable layout: section table outside readable headers.");
    std::vector<IMAGE_SECTION_HEADER> sections(file.NumberOfSections);
    std::memcpy(sections.data(),static_cast<const std::byte*>(image)+section_table,table_size);
    std::sort(sections.begin(),sections.end(),[](const auto& a,const auto& b){return a.VirtualAddress<b.VirtualAddress;});
    std::size_t previous_end=optional.SizeOfHeaders;
    for(const auto& section:sections) {
        const auto size=(std::max)(section.Misc.VirtualSize,section.SizeOfRawData);
        if(!size||section.VirtualAddress<previous_end||section.VirtualAddress%optional.SectionAlignment||
           section.VirtualAddress>optional.SizeOfImage||size>optional.SizeOfImage-section.VirtualAddress)
            return fail("Refused: unsupported executable layout: overlapping or out-of-image section.");
        previous_end=static_cast<std::size_t>(section.VirtualAddress)+size;
    }
    for(const auto& required:profile.sections) {
        const auto found=std::find_if(sections.begin(),sections.end(),[&](const auto& s){return s.VirtualAddress==required.rva;});
        if(found==sections.end()||found->Misc.VirtualSize<required.minimum_size||!section_access(found->Characteristics,required.access))
            return fail(position(required.name,required.rva,"unsupported section layout"));
    }
    auto range_valid=[&](std::size_t rva,std::size_t size,ImageAccess access) {
        if(!size||rva>optional.SizeOfImage||size>optional.SizeOfImage-rva) return false;
        for(const auto& section:sections) {
            if(rva<section.VirtualAddress) continue;
            const auto offset=rva-section.VirtualAddress;
            const auto length=(std::max)(section.Misc.VirtualSize,section.SizeOfRawData);
            if(offset<=length&&size<=length-offset&&section_access(section.Characteristics,access))
                return memory_access(image,rva,size,access);
        }
        return false;
    };
    for(const auto& range:profile.ranges)
        if(!range_valid(range.rva,range.size,range.access))
            return fail(position(range.name,range.rva,"required range is out of bounds or has incompatible memory access"));
    for(const auto& site:profile.sites) {
        const auto length=std::strlen(site.expected);
        if(!length||length%2) return fail(position(site.name,site.rva,"invalid expected bytes"));
        std::vector<unsigned char> expected;expected.reserve(length/2);
        for(std::size_t i=0;i<length;i+=2) {
            const auto high=hex_digit(site.expected[i]),low=hex_digit(site.expected[i+1]);
            if(high<0||low<0) return fail(position(site.name,site.rva,"invalid expected bytes"));
            expected.push_back(static_cast<unsigned char>(high*16+low));
        }
        if(!range_valid(site.rva,expected.size(),site.access))
            return fail(position(site.name,site.rva,"required bytes are out of bounds or unreadable"));
        const auto actual=reinterpret_cast<const unsigned char*>(image)+site.rva;
        const auto mismatch=std::mismatch(expected.begin(),expected.end(),actual);
        if(mismatch.first!=expected.end()) {
            const auto offset=static_cast<std::size_t>(mismatch.first-expected.begin());
            std::ostringstream out;out<<position(site.name,site.rva+offset,"required instruction/data mismatch")
                <<" Expected byte: 0x"<<std::hex<<static_cast<unsigned>(*mismatch.first)
                <<"; actual byte: 0x"<<static_cast<unsigned>(*mismatch.second)<<'.';
            return fail(out.str());
        }
        ++result.checked_sites;
    }
    result.compatible=true;return result;
}
ExecutableHash executable_hash(const std::filesystem::path& path) {
    ExecutableHash result;
    auto fail=[&](const char* error){result.error=error;return result;};
    std::ifstream stream(path,std::ios::binary);
    if(!stream) return fail("Executable SHA-256 unavailable: cannot open executable file.");
    HashHandles handles;
    if(BCryptOpenAlgorithmProvider(&handles.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        return fail("Executable SHA-256 unavailable: cannot open SHA-256 provider.");
    DWORD object_size=0,returned=0;
    if(BCryptGetProperty(handles.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&returned,0)<0||
       returned!=sizeof(object_size)||!object_size)
        return fail("Executable SHA-256 unavailable: cannot query hash object size.");
    handles.object.resize(object_size);
    if(BCryptCreateHash(handles.algorithm,&handles.hash,handles.object.data(),object_size,nullptr,0,0)<0)
        return fail("Executable SHA-256 unavailable: cannot create hash.");
    std::array<char,65536> buffer{};
    for(;;) {
        stream.read(buffer.data(),buffer.size());
        if(stream.bad()||(!stream.eof()&&stream.fail()))
            return fail("Executable SHA-256 unavailable: executable file read failed.");
        if(stream.gcount()&&BCryptHashData(handles.hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(stream.gcount()),0)<0)
            return fail("Executable SHA-256 unavailable: cannot hash executable bytes.");
        if(stream.eof()) break;
    }
    std::array<UCHAR,32> digest{};
    if(BCryptFinishHash(handles.hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)
        return fail("Executable SHA-256 unavailable: cannot finish hash.");
    constexpr char digits[]="0123456789abcdef";
    for(auto byte:digest) {result.sha256+=digits[byte>>4];result.sha256+=digits[byte&15];}
    return result;
}
}
