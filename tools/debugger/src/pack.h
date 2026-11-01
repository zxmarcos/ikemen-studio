// A zip file of a project's files, stored: SFFs and sounds are compressed already, or do not gain much.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PackFile {
    std::string name, data; // name: with '/', relative to the game's folder
};

uint32_t Zip_Crc32(const std::string &data);
std::string Zip_Build(const std::vector<PackFile> &files);
