#include "executable_compatibility.hpp"
#include <windows.h>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct Fixture {
    std::byte* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    IMAGE_DOS_HEADER* dos=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;IMAGE_SECTION_HEADER* sections=nullptr;
    Fixture() {
        require(image!=nullptr,"Allocate test image");
        dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
        nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.NumberOfSections=4;
        nt->FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;
        nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfHeaders=0x1000;
        nt->OptionalHeader.SizeOfImage=0x5000;nt->OptionalHeader.SectionAlignment=0x1000;
        sections=IMAGE_FIRST_SECTION(nt);
        for(unsigned i=0;i<4;++i) {
            sections[i].VirtualAddress=(i+1)*0x1000;sections[i].Misc.VirtualSize=0x1000;
            sections[i].SizeOfRawData=0x1000;sections[i].Characteristics=IMAGE_SCN_MEM_READ;
        }
        sections[0].Characteristics|=IMAGE_SCN_MEM_EXECUTE;sections[2].Characteristics|=IMAGE_SCN_MEM_WRITE;
        image[0x1010]=std::byte{0x48};image[0x1011]=std::byte{0x89};image[0x2010]=std::byte{8};
        protect(0x1000,PAGE_EXECUTE_READWRITE);
    }
    ~Fixture() {if(image) VirtualFree(image,0,MEM_RELEASE);}
    void protect(std::size_t offset,DWORD access) {
        DWORD before=0;require(VirtualProtect(image+offset,0x1000,access,&before)!=0,"Protect test page");
    }
};
eu4menu::ImageProfile profile() {
    using eu4menu::ImageAccess;
    return {"test layout",{{0x1000,0x1000,ImageAccess::code,"text"},{0x2000,0x1000,ImageAccess::read,"constants"},
        {0x3000,0x1000,ImageAccess::write,"globals"}},
        {{0x1010,"4889","test hook"},{0x2010,"08","constant",ImageAccess::read}},
        {{0x3010,8,ImageAccess::write,"manager"}}};
}
void menu_profile_tests() {
    const auto& p=eu4menu::eu4_1375_profile();
    const auto& last=p.sections.back();
    const auto size=(last.rva+last.minimum_size+0xfff)&~std::size_t{0xfff};
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    require(image!=nullptr,"Allocate menu fixture");
    struct Release {void* image;~Release(){VirtualFree(image,0,MEM_RELEASE);}} release{image};
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+0x80);
    nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;
    nt->FileHeader.NumberOfSections=static_cast<WORD>(p.sections.size());
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfHeaders=0x1000;nt->OptionalHeader.SizeOfImage=static_cast<DWORD>(size);
    nt->OptionalHeader.SectionAlignment=0x1000;
    auto* sections=IMAGE_FIRST_SECTION(nt);
    for(std::size_t i=0;i<p.sections.size();++i) {
        const auto& s=p.sections[i];
        sections[i].VirtualAddress=static_cast<DWORD>(s.rva);
        sections[i].Misc.VirtualSize=static_cast<DWORD>(s.minimum_size);
        sections[i].Characteristics=IMAGE_SCN_MEM_READ;
        if(s.access==eu4menu::ImageAccess::code) sections[i].Characteristics|=IMAGE_SCN_MEM_EXECUTE;
        if(s.access==eu4menu::ImageAccess::write) sections[i].Characteristics|=IMAGE_SCN_MEM_WRITE;
    }
    for(const auto& site:p.sites) {
        for(std::size_t i=0;i<std::strlen(site.expected);i+=2) {
            char pair[]{site.expected[i],site.expected[i+1],0};
            image[site.rva+i/2]=static_cast<std::byte>(std::strtoul(pair,nullptr,16));
        }
    }
    auto* frontend=reinterpret_cast<std::uintptr_t*>(image+0x1d5a8e8);
    *frontend=reinterpret_cast<std::uintptr_t>(image)+0x10d2e80;
    DWORD previous=0;
    const auto& code=p.sections.front();
    require(VirtualProtect(image+code.rva,(code.minimum_size+0xfff)&~std::size_t{0xfff},PAGE_EXECUTE_READWRITE,&previous)!=0,"Protect menu code");
    require(eu4menu::check_menu_image(image).compatible,"Menu profile passes");
    for(const auto& site:p.sites) {
        const auto offset=site.rva+std::strlen(site.expected)/4;
        const auto before=image[offset];image[offset]^=std::byte{1};
        require(!eu4menu::check_menu_image(image).compatible,"Every menu dependency rejects a code conflict");
        image[offset]=before;
    }
    ++*frontend;
    const auto pointer_conflict=eu4menu::check_menu_image(image);
    require(!pointer_conflict.compatible&&pointer_conflict.error.find("frontend tick pointer mismatch")!=std::string::npos,"Frontend pointer conflict rejected");
    --*frontend;
    require(eu4menu::check_menu_image(image).compatible,"Menu fixture recovers after conflicts");
}
}
int main() {
    try {
        menu_profile_tests();
        Fixture f;auto p=profile();
        const auto check=[&]{return eu4menu::check_executable_image(f.image,p);};
        require(check().compatible&&check().checked_sites==2,"Supported mapped image passes");
        std::array<std::byte,0x5000> before{};std::memcpy(before.data(),f.image,before.size());
        check();require(std::memcmp(before.data(),f.image,before.size())==0,"Preflight never modifies image");
        f.image[0x4010]=std::byte{0xff};require(check().compatible,"Unrelated resource change passes");
        f.image[0x1080]=std::byte{0x90};require(check().compatible,"Unrelated code change passes");
        f.nt->OptionalHeader.CheckSum=123;f.nt->OptionalHeader.ImageBase=0x180000000;
        require(check().compatible,"PE checksum and preferred address are not compatibility gates");
        f.nt->FileHeader.NumberOfSections=5;f.nt->OptionalHeader.SizeOfImage=0x6000;
        f.sections[4]=f.sections[2];f.sections[4].VirtualAddress=0x5000;
        require(check().compatible,"New non-overlapping section and larger image pass");
        f.sections[4].VirtualAddress=0x3000;require(!check().compatible,"Overlapping added section rejected");
        f.sections[4].VirtualAddress=0x5000;
        f.image[0x1011]=std::byte{0x90};const auto conflict=check();
        require(!conflict.compatible&&conflict.error.find("test hook at RVA 0x1011")!=std::string::npos&&
            conflict.error.find("actual byte: 0x90")!=std::string::npos,"Critical code reports exact mismatch");
        f.image[0x1011]=std::byte{0x89};f.image[0x2010]=std::byte{9};
        require(!check().compatible,"Critical data change rejected");f.image[0x2010]=std::byte{8};
        f.nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;require(!check().compatible,"x86 rejected");
        f.nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;f.nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        require(!check().compatible,"PE32 rejected");f.nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        f.nt->FileHeader.Characteristics|=IMAGE_FILE_DLL;require(!check().compatible,"DLL image rejected");
        f.nt->FileHeader.Characteristics&=~IMAGE_FILE_DLL;
        f.sections[0].VirtualAddress=0x6000;require(!check().compatible,"Out-of-image section rejected");
        f.sections[0].VirtualAddress=0x1000;
        f.sections[0].Characteristics=IMAGE_SCN_MEM_READ;require(!check().compatible,"Non-executable code section rejected");
        f.sections[0].Characteristics|=IMAGE_SCN_MEM_EXECUTE;
        p.sites[0].rva=0x9000;require(!check().compatible,"Out-of-image site rejected before dereference");
        p=profile();p.ranges[0].size=(std::numeric_limits<std::size_t>::max)();
        require(!check().compatible,"Overflowing required range rejected");p=profile();
        f.protect(0x1000,PAGE_NOACCESS);require(!check().compatible,"Unreadable code rejected without access violation");
        f.protect(0x1000,PAGE_GUARD|PAGE_EXECUTE_READWRITE);require(!check().compatible,"Guard page rejected without consuming guard");
        MEMORY_BASIC_INFORMATION region{};VirtualQuery(f.image+0x1000,&region,sizeof(region));
        require((region.Protect&PAGE_GUARD)!=0,"Preflight did not access guard page");
        f.protect(0x1000,PAGE_READWRITE);require(!check().compatible,"Non-executable memory rejected");
        f.protect(0x1000,PAGE_EXECUTE_READWRITE);
        f.protect(0x3000,PAGE_READONLY);require(!check().compatible,"Read-only global storage rejected");
        f.protect(0x3000,PAGE_READWRITE);
        f.nt->FileHeader.NumberOfSections=97;require(!check().compatible,"Excessive section count rejected");
        f.nt->FileHeader.NumberOfSections=5;f.nt->OptionalHeader.SizeOfHeaders=0x100;
        require(!check().compatible,"Truncated headers rejected");f.nt->OptionalHeader.SizeOfHeaders=0x1000;
        f.dos->e_lfanew=(std::numeric_limits<LONG>::max)();require(!check().compatible,"Overflowing NT header address rejected");
        f.dos->e_lfanew=0x80;f.protect(0,PAGE_NOACCESS);
        require(!check().compatible,"Unreadable DOS header rejected");f.protect(0,PAGE_READWRITE);
        require(!eu4menu::check_executable_image(nullptr,p).compatible,"Null image rejected");
        require(!eu4menu::check_executable_image(reinterpret_cast<void*>(1),p).compatible,"Invalid image pointer rejected");
        require(check().compatible,"Valid image still passes after rejected cases");
        const auto path=std::filesystem::temp_directory_path()/("eu4-menu-hash-"+std::to_string(GetCurrentProcessId())+".tmp");
        {std::ofstream out(path,std::ios::binary);out<<"abc";}
        const auto hash=eu4menu::executable_hash(path);
        std::filesystem::remove(path);
        require(hash.error.empty()&&hash.sha256=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 diagnostic is correct");
        const auto missing=eu4menu::executable_hash(path);
        require(missing.sha256.empty()&&missing.error.find("cannot open")!=std::string::npos,"File error has a distinct diagnostic");
        require(check().compatible,"Diagnostic file failure does not change image compatibility");
        std::cout<<"Executable compatibility: layout, unrelated changes, conflicts, bounds, protected pages and diagnostic hashing passed.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
