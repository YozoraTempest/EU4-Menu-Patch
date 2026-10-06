#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace eu4menu {
enum class ImageAccess { code, read, write };
struct ImageSection {
    std::size_t rva, minimum_size;
    ImageAccess access;
    const char* name;
};
struct ImageSite {
    std::size_t rva;
    const char* expected;
    const char* name;
    ImageAccess access=ImageAccess::code;
};
struct ImageRange {
    std::size_t rva, size;
    ImageAccess access;
    const char* name;
};
struct ImageProfile {
    const char* name;
    std::vector<ImageSection> sections;
    std::vector<ImageSite> sites;
    std::vector<ImageRange> ranges;
};
struct ImageCheck {
    bool compatible=false;
    std::size_t checked_sites=0;
    std::string error;
};
struct ExecutableHash {
    std::string sha256, error;
};
const ImageProfile& eu4_1375_profile();
ImageCheck check_menu_image(const void* image);
// Reads the mapped executable; never changes its bytes or runs its entry point.
ImageCheck check_executable_image(const void* image, const ImageProfile& profile);
// Diagnostic only. A failed file hash does not change image compatibility.
ExecutableHash executable_hash(const std::filesystem::path& file);
}
