#pragma once

#include <iostream>
#include <iomanip>
#include <cassert>

#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <zlib.h>
#include <cstring>

namespace ShittyCodeForReadingMinecraft
{
//==============================================================================
    struct iVec2
    {
        int x, z;
    };
    struct iVec3
    {
        int x, y, z;
    };
    struct Mapping
    {
        void *data;
        size_t size;
    };

//==============================================================================
    Mapping mapFile(const std::string &file_name)
    {
        const auto file_descriptor = open(file_name.c_str(), O_RDONLY);

        if (file_descriptor == -1)
            return {nullptr, 0};

        struct stat file_info;
        const auto result1 = fstat(file_descriptor, &file_info);

        if (result1 == -1)
            return {nullptr, 0};

        void *mapped_pointer = mmap(
            nullptr,
            (size_t) file_info.st_size,
            PROT_READ,
            MAP_PRIVATE,
            file_descriptor,
            0
        );

        close(file_descriptor);

        if (mapped_pointer == reinterpret_cast<void *>(-1))
            return {nullptr, 0};
        else
            return {mapped_pointer, (size_t) file_info.st_size};
    }

//==============================================================================
    int floorMod(int x, int y)
    { return (x % y + y) % y; }

    int floorDiv(int x, int y)
    { return (x + (x < 0)) / y - (x < 0); }

//==============================================================================
    int toIndex(iVec2 pos, iVec2 dim)
    {
        pos.x = floorMod(pos.x, dim.x);
        pos.z = floorMod(pos.z, dim.z);

        return pos.z * dim.x + pos.x;
    }

//==============================================================================
    int toIndex3(iVec3 pos, iVec3 dim)
    {
        pos.x = floorMod(pos.x, dim.x);
        pos.y = floorMod(pos.y, dim.y);
        pos.z = floorMod(pos.z, dim.z);

        return pos.z * dim.y * dim.x + pos.y * dim.x + pos.x;
    }

//==============================================================================
    void unmapFile(Mapping mapped_file)
    {
        munmap(mapped_file.data, mapped_file.size);
    }

//==============================================================================
    int bigToLittleEndian(const char *src, int bytes)
    {
        if (bytes > 4 || bytes < 1) throw 1;

        int result = 0;

        for (int i = 0; i < bytes; ++i)
            result |= ((int) src[i] & 0xff) << (8 * (bytes - (i + 1)));

        return result;
    }

//==============================================================================
    size_t big2lit(const char *src, int bytes)
    {
        return (size_t) bigToLittleEndian(src, bytes);
    }

//==============================================================================
    iVec2 chunkPositionToRegionPosition(const iVec2 chunk_position)
    {
        // floor division by 32
        return {chunk_position.x >> 5, chunk_position.z >> 5}; // alternatively floorDiv(pos, dim)
    }

//==============================================================================
    iVec2 blockPositionToChunkPosition(const iVec2 block_position)
    {
        // floor division by 16
        return {block_position.x >> 4, block_position.z >> 4};  // alternatively floorDiv(pos, dim)
    }

//==============================================================================
    constexpr size_t BUFFER_SIZE{
        1024 * 1024}; // "Chunks will always be less than 1MiB in size." - minecraft.gamepedia.com

//==============================================================================
    std::unique_ptr<char[]> getChunkNBT(const iVec2 chunk_position, const std::string & source_folder)
    {
        const auto region_position = chunkPositionToRegionPosition(chunk_position);
        const std::string source_file{
            source_folder + "r." + std::to_string(region_position.x) + "." + std::to_string(region_position.z) +
            ".mca"};

        auto file = mapFile(source_file);

        if (file.data == nullptr) return {};

        const char *locations = reinterpret_cast<char *>(file.data);
        const char *timestamps = reinterpret_cast<char *>(file.data) + 4096;

        const iVec2 dim{32, 32};
        iVec2 pos = {floorMod(chunk_position.x, dim.x), floorMod(chunk_position.z, dim.z)};

        std::unique_ptr<char[]> uncompressed;

//    for (pos.z = 0; pos.z < dim.z; ++pos.z)
//        for (pos.x = 0; pos.x < dim.x; ++pos.x)
//        {
        const int index = toIndex(pos, dim);

        const char *p_location = locations + 4 * index;
        const char *p_timestamp = timestamps + 4 * index;

        const int offset = bigToLittleEndian(p_location, 3);
        const int sector_count = bigToLittleEndian(p_location + 3, 1);
        const int time_stamp = bigToLittleEndian(p_timestamp, 4);

//            if (offset == 0) continue; // chunk not saved
        if (offset != 0)
        {
            if (offset == 1)
                throw 1;  // can only start at offset >= 2

            const char *chunk_data = reinterpret_cast<char *>(file.data) + offset * 4096;
            const int length = bigToLittleEndian(chunk_data, 4);
            const int compression_type = bigToLittleEndian(chunk_data + 4, 1);
            const char *compressed_data = chunk_data + 5;

            if (compression_type != 2)
                throw 1; // unsupported compression type

#if 0
            std::cout
                << std::setfill(' ') << std::setw(2) << pos.x << " | "
                << std::setfill(' ') << std::setw(2) << pos.z << " | "
                << std::setfill(' ') << std::setw(8) << offset << " | "
                << std::setfill(' ') << std::setw(3) << sector_count << " | "
                << std::setfill(' ') << std::setw(10) << time_stamp << " | "
                << std::setfill(' ') << std::setw(10) << length << " | "
                << std::setfill(' ') << std::setw(1) << compression_type << std::endl;
#endif

            uncompressed = std::make_unique<char[]>(BUFFER_SIZE);
            uLongf destination_length{BUFFER_SIZE};

            const auto uncompression_result = uncompress(
                reinterpret_cast<Bytef *>(uncompressed.get()), &destination_length,
                reinterpret_cast<const Bytef *>(compressed_data), static_cast<uLongf>(length)
            );

            if (uncompression_result != Z_OK)
                throw 1; // failed to uncompress
        }
//        }

        unmapFile(file);

        return uncompressed;
    }

//==============================================================================
    void toHex(char c, char out[2])
    {
        char hex[]{
            '0', '1', '2', '3',
            '4', '5', '6', '7',
            '8', '9', 'A', 'B',
            'C', 'D', 'E', 'F'
        };

        out[0] = hex[c >> 4 & 0xf];
        out[1] = hex[c & 0xf];
    }

//==============================================================================
    char sanitize(char c)
    {
        if (c >= 33 && c <= 126)
            return c;
        else
            return ' ';

    }

//==============================================================================
    void printData(const char *data, const size_t size)
    {
        const int buffer_mod = 16;
        char buffer[]{"| 00 00 00 00 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00 | 0123456789ABCDEF |\n"};
        int buffer_hex_index[]{2, 5, 8, 11, 16, 19, 22, 25, 30, 33, 36, 39, 44, 47, 50, 53};
        int buffer_chr_index[]{58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73};

        auto clearBuffer = [&]()
        {
            for (int x = 0; x < buffer_mod; ++x)
            {
                buffer[buffer_hex_index[x]] = ' ';
                buffer[buffer_hex_index[x] + 1] = ' ';
                buffer[buffer_chr_index[x]] = ' ';
            }
        };

        clearBuffer();

        for (size_t i = 0; i < size; ++i)
        {
            const char c = data[i];
            const size_t index = i % buffer_mod;
            toHex(c, &buffer[buffer_hex_index[index]]);
            buffer[buffer_chr_index[index]] = sanitize(c);

            if (index == buffer_mod - 1)
            {
                std::cout << buffer;
                clearBuffer();
            }
        }
        if (size % buffer_mod != 0)
            std::cout << buffer;
    }

//==============================================================================
    char *testPrintCoordinates(const char *data, const int Y)
    {
        /*
         *  NBT format:
         *
         *  byte   1: tag_type
         *  byte 2-3: name_length
         *  byte 4-n: name in UTF-8 of size name_length
         *  byte n+1-m: data
         *
         *  TAG_end is an exception
         *
         */

        if (Y < 0 || Y > 15) return nullptr;

        // this code is at least as bad as the NBT file structure


        // TODO: support UTF-8 and not only ASCII part

        constexpr char TAG_End{0};
        constexpr char TAG_Byte{1};
        constexpr char TAG_Short{2};
        constexpr char TAG_Int{3};
        constexpr char TAG_Long{4};
        constexpr char TAG_Float{5};
        constexpr char TAG_Double{6};
        constexpr char TAG_Byte_Array{7};
        constexpr char TAG_String{8};
        constexpr char TAG_List{9};
        constexpr char TAG_Compound{10};
        constexpr char TAG_Int_Array{11};

        constexpr size_t TAG_End_s{0};
        constexpr size_t TAG_Byte_s{1};
        constexpr size_t TAG_Short_s{2};
        constexpr size_t TAG_Int_s{4};
        constexpr size_t TAG_Long_s{8};
        constexpr size_t TAG_Float_s{4};
        constexpr size_t TAG_Double_s{8};

        constexpr size_t TAG_Byte_Array_s{TAG_Int_s};
        constexpr size_t TAG_String_s{TAG_Short_s};
        constexpr size_t TAG_List_s{1 + TAG_Int_s};
        constexpr size_t TAG_Compound_s{0};
        constexpr size_t TAG_Int_Array_s{TAG_Int_s};

        size_t i = 0;

        /*if (data[i] != TAG_Compound) return; // root tag
        i += 1;
        if (big2lit(&data[i], 2) != 0) return; // root tag name is empty
        i += 2;*/

        int y = -1;
        int y_index = -1;
        int b_index = -1;
        const char *b_data = nullptr;
        bool sections_started = false;

        // find Level tag
        while (true)
        {
            if (i > (1024 * 1024 - 10)) // we have a bug in NBT reading. this will at least prevent 90% of segmentation faults
            {
                return nullptr;
            }

            const char type = data[i];
            const size_t name_len = big2lit(&data[i + 1], 2);
            const char *name = &data[i + 3];
            const char *content = &data[i + 3 + name_len];
            // TODO: figure out another off by one @ Entities?

            if (sections_started)
            {
                if (
                    (name_len == 1 && std::strncmp(name, "Y", 1) == 0) ||
                    (name_len == 6 && std::strncmp(name, "Blocks", 6) == 0) ||
                    (name_len == 3 && std::strncmp(name, "Add", 3) == 0) ||

                    (name_len == 4 && std::strncmp(name, "Data", 4) == 0) ||
                    (name_len == 10 && std::strncmp(name, "BlockLight", 10) == 0) ||
                    (name_len == 8 && std::strncmp(name, "SkyLight", 8) == 0) ||
                    (name_len == 0 && type == TAG_Compound)

                    )
                    (void) 0;
                else
                    return nullptr;
            }

            switch (type)
            {
                case TAG_End:
                    i += 3 + name_len + TAG_End_s;
                    break;
                case TAG_Byte:
                    i += 3 + name_len + TAG_Byte_s;
                    if (name_len == 1 && std::strncmp(name, "Y", 1) == 0)
                    {
                        y_index += 1;
                        y = (int) (*content);

                        if (b_index == y_index && Y == y)
                            return const_cast<char *>(b_data);
                    }
                    break;
                case TAG_Short:
                    i += 3 + name_len + TAG_Short_s;
                    break;
                case TAG_Int:
                    i += 3 + name_len + TAG_Int_s;
                    break;
                case TAG_Long:
                    i += 3 + name_len + TAG_Long_s;
                    break;
                case TAG_Float:
                    i += 3 + name_len + TAG_Float_s;
                    break;
                case TAG_Double:
                    i += 3 + name_len + TAG_Double_s;
                    break;

                case TAG_Byte_Array:
                    i += 3 + name_len + TAG_Byte_Array_s + big2lit(content, TAG_Int_s) * 1;
                    if (name_len == 4 && std::strncmp(name, "Data", 4) == 0)
                        i += 1; // TODO: figure out what's going on. does Minecraft have an off by 1 error?
                    else if (name_len == 6 && std::strncmp(name, "Blocks", 6) == 0)
                    {
                        b_index += 1;
                        b_data = content + 4;

                        if (b_index == y_index && Y == y)
                            return const_cast<char *>(b_data);
                    }
                    break;
                case TAG_String:
                    i += 3 + name_len + TAG_String_s + big2lit(content, TAG_Short_s) * 1;
                    break; // TODO: figure out: is the size in characters or bytes (UTF-8)
                case TAG_List:
                    i += 3 + name_len + TAG_List_s;
                    if (name_len == 8 && std::strncmp(name, "Sections", 8) == 0)
                        sections_started = true;
                    break;
                case TAG_Compound:
                    i += 3 + name_len + TAG_Compound_s;
                    break;
                case TAG_Int_Array:
                    i += 3 + name_len + TAG_Int_Array_s + big2lit(content, TAG_Int_s) * 4;
                    break;
                default:
                    i += 3 + name_len + TAG_Double_s;
                    std::cout << "Figure out why this is reached." << std::endl;
                    return nullptr;
                    throw 1;
                    break;
            }
            /*
            if (data[i] == TAG_Compound) // Level is TAG_Compound
            {
                i += 1;
                if (big2lit(&data[i], 2) == 5) // Level name is 5 characters
                {
                    i += 2;
                    if (std::memcmp(&data[i], "Level", 5) == 0) continue; // wrong tag
                    i += 1;
                }
            }

            break;*/
        }
    }

//==============================================================================
    constexpr size_t BLOCK_DATA_SIZE{4096};

//==============================================================================
    std::unique_ptr<char[]> getChunkData(iVec3 chunk_coordinate, const std::string & source_dir)
    {
        if (chunk_coordinate.y < 0 || chunk_coordinate.y > 15)
        {
            std::unique_ptr<char[]> copy_empty{std::make_unique<char[]>(BLOCK_DATA_SIZE)};
            for (size_t i = 0; i < BLOCK_DATA_SIZE; ++i)
                copy_empty[i] = 0;
            return copy_empty;
        }

        auto chunk_NBT = getChunkNBT({chunk_coordinate.x, chunk_coordinate.z}, source_dir);
        const char *data = chunk_NBT.get();

        if (data == nullptr) // chunk not found
        {
            std::unique_ptr<char[]> copy_empty{std::make_unique<char[]>(BLOCK_DATA_SIZE)};
            for (size_t i = 0; i < BLOCK_DATA_SIZE; ++i)
                copy_empty[i] = 0;
            return copy_empty;
        }

        /*size_t size = BUFFER_SIZE - 1;

        while (size != 0 && data[size] == '\0')
            --size; // find last non null byte
        size += 1;

        printData(data, size);*/

        auto * block_data = testPrintCoordinates(data, chunk_coordinate.y);

        if (block_data == nullptr) // chunk not found
        {
            std::unique_ptr<char[]> copy_empty{std::make_unique<char[]>(BLOCK_DATA_SIZE)};
            for (size_t i = 0; i < BLOCK_DATA_SIZE; ++i)
                copy_empty[i] = 0;
            return copy_empty;
        }

        std::unique_ptr<char[]> copy{std::make_unique<char[]>(BLOCK_DATA_SIZE)};

        //std::memcpy(copy.get(), block_data, BLOCK_DATA_SIZE);

        iVec3 pos;
        const iVec3 dim{16, 16, 16};
        for (pos.z = 0; pos.z < 16; ++pos.z)
            for (pos.y = 0; pos.y < 16; ++pos.y)
                for (pos.x = 0; pos.x < 16; ++pos.x)
                    copy[toIndex3({pos.x, pos.y, pos.z}, dim)] = block_data[toIndex3({pos.x, pos.z, pos.y}, dim)];

//        for (size_t i = 0; i < BLOCK_DATA_SIZE; ++i)
//            if (copy[i] != 0) copy[i] = 1;

        return copy;
    }
}
