#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include "hashmap_builder.hpp"
#include "hasher.hpp"

namespace statehashmap
{

constexpr size_t BLOCK_SIZE = 4 * 1024; //* 1024; // 4MB
constexpr size_t HASH_SIZE = 32;
constexpr size_t MAX_HASHES = BLOCK_SIZE / HASH_SIZE;
constexpr size_t EXT_LEN = 7;
const char *const HASHMAP_EXT = ".bhmap";
const char *const BLOCKINDEX_EXT = ".bindex";

hashmap_builder::hashmap_builder(std::string statedir, std::string cachedir, std::string hashmapdir)
{
    this->statedir = statedir;
    this->hashmapdir = hashmapdir;
    this->cachedir = cachedir;
}

int hashmap_builder::generate(std::list<std::string> filepathhints)
{
    // if filepath hints are not provided, simply generate hash map for the entire statedir recursively.
    if (filepathhints.empty())
    {
        boost::filesystem::recursive_directory_iterator itr(statedir);
        boost::filesystem::recursive_directory_iterator itrend;
        for (boost::system::error_code ec; itr != itrend;)
        {
            itr.increment(ec);
            const boost::filesystem::path path = itr->path();
            if (ec)
            {
                std::cerr << "Error While Accessing : " << path.string() << " :: " << ec.message() << '\n';
                return -1;
            }

            if (boost::filesystem::is_regular_file(path))
                generate_hashmap_forfile(path.string());
        }
    }
    else
    {
        for (std::string_view filepath : filepathhints)
            generate_hashmap_forfile(filepath);
    }

    return 0;
}

int hashmap_builder::generate_hashmap_forfile(std::string_view filepath)
{
    // We attempt to avoid a full rebuild of the hash map file when possible.
    // For this optimisation, both the block hash map (.bhmap) file and the
    // block cache index (.bindex) files must exist.

    // If the block index exists, we generate/update the hashmap file with the aid of that.
    // Block index file contains the total length of original file and updated block hashes.
    // If not, we simply read the original file and recalculate all the block hashes.

    std::string_view relpath = filepath.substr(statedir.length(), filepath.length() - statedir.length());
    std::string hmapfile;
    hmapfile.reserve(hashmapdir.length() + relpath.length() + EXT_LEN);
    hmapfile.append(hashmapdir).append(relpath).append(HASHMAP_EXT);

    // Attempt to read the block index file.
    off_t orifilelength = 0;
    std::map<uint32_t, hasher::B2H> bindex;
    get_blockindex(bindex, orifilelength, relpath);

    const bool bindex_exists = !bindex.empty();

    int orifd = 0;

    // If block index does not exist, we'll need to read raw data blocks from the original file.
    if (!bindex_exists)
    {
        orifd = open(filepath.data(), O_RDONLY);
        if (orifd == -1)
        {
            std::cerr << "Open failed " << filepath << '\n';
            return -1;
        }

        // Detect the original file length if not already loaded by bindex file.
        if (orifilelength == 0)
            orifilelength = lseek(orifd, 0, SEEK_END);
    }

    uint32_t blockcount = ceil((double)orifilelength / (double)BLOCK_SIZE);
    std::map<uint32_t, hasher::B2H> hashes;

    for (uint32_t blockid = 0; blockid < blockcount; blockid++)
    {
        if (bindex_exists)
        {
            const auto itr = bindex.find(blockid);
            if (itr != bindex.end())
            {
                hashes[blockid] = itr->second;
            }
        }
        else
        {
            // If bindex does not exist, calculate the hash using raw data block.
            char block[BLOCK_SIZE];
            off_t blockoffset = BLOCK_SIZE * blockid;
            if (pread(orifd, block, BLOCK_SIZE, blockoffset) != 0)
            {
                std::cerr << "Read failed " << filepath << '\n';
                return -1;
            }

            hashes[blockid] = hasher::hash(&blockoffset, 8, block, BLOCK_SIZE);
        }
    }

    // Calculate root hash.
    

    int hmapfd = open(hmapfile.data(), O_RDONLY | O_CREAT | O_TRUNC, 0644);
    if (hmapfd == -1)
    {
        std::cerr << "Open failed " << hmapfile << '\n';
        return -1;
    }

    return 0;
}

int hashmap_builder::get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, off_t &orifilelength, std::string_view filerelpath)
{
    std::string bindexfile;
    bindexfile.reserve(cachedir.length() + filerelpath.length() + EXT_LEN);
    bindexfile.append(cachedir).append(filerelpath).append(BLOCKINDEX_EXT);

    if (!boost::filesystem::exists(bindexfile))
    {
        std::ifstream infile(bindexfile, std::ios::binary | std::ios::ate);
        std::streamsize idxsize = infile.tellg();
        infile.seekg(0, std::ios::beg);

        // Read the block index file into a vector.
        std::vector<char> bindex(idxsize);
        if (infile.read(bindex.data(), idxsize))
        {
            //  First 8 bytes contain the original file length.
            memcpy(&orifilelength, bindex.data(), 8);

            // Skip the first 8 bytes and loop through index entries.
            for (uint32_t idxoffset = 8; idxoffset < bindex.size();)
            {
                // Read the block no. (4 bytes) of where this block is from in the original file.
                uint32_t blockno = 0;
                memcpy(&blockno, bindex.data() + idxoffset, 4);
                idxoffset += 12; // Skip the cached block offset

                // Read the block hash (32 bytes).
                hasher::B2H hash;
                memcpy(&hash, bindex.data() + idxoffset, 32);

                idxmap.try_emplace(blockno, hash);
            }
        }
        else
        {
            std::cerr << "Read failed " << bindexfile << '\n';
        }

        infile.close();
    }

    return 0;
}

int hashmap_builder::insert_blockhash(const int hmapfd, const int blockid, void *hashbuf)
{
    if (pwrite(hmapfd, hashbuf, HASH_SIZE, blockid * BLOCK_SIZE) == -1)
        return -1;
    return 0;
}

} // namespace statehashmap