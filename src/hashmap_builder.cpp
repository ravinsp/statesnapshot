#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <boost/filesystem.hpp>
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
        for (boost::system::error_code ec; itr != itrend; itr++)
        {
            const boost::filesystem::path path = itr->path();
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

    uint32_t blockcount = 0;
    int orifd = 0, hmapfd = 0;
    bool oldhmap_exists;

    if (open_blockhashmap(hmapfd, oldhmap_exists, relpath) != 0)
        return -1;

    // Attempt to read the block index file.
    std::map<uint32_t, hasher::B2H> bindex;
    if (get_blockindex(bindex, blockcount, relpath) != 0)
        return -1;

    // No need to read original file if the block index has all the blocks information.
    // If block index is not sufficient, we'll need to read raw data blocks from the original file.
    if (bindex.empty() || bindex.size() < blockcount)
    {
        orifd = open(filepath.data(), O_RDONLY);
        if (orifd == -1)
        {
            std::cerr << "Open failed " << filepath << '\n';
            return -1;
        }

        // Detect the block count if not already loaded by block index file.
        if (blockcount == 0)
        {
            off_t orifilelength = lseek(orifd, 0, SEEK_END);
            blockcount = ceil((double)orifilelength / (double)BLOCK_SIZE);
        }
    }

    // Build up the latest block hash list in memory.
    // Hashes maybe fetched from block index or existing hash map (if available) or
    // recalculated from original file.
    hasher::B2H hashes[1 + blockcount]; // +1 is for the root hash.
    size_t newhmap_filesize = (1 + blockcount) * HASH_SIZE;

    get_updatedhashes(hashes, relpath, oldhmap_exists, hmapfd, orifd, blockcount, bindex, newhmap_filesize);

    // Calculate the root hash (we use XOR for this).
    hasher::B2H roothash;
    for (hasher::B2H hash : hashes)
    {
        roothash.data[0] ^= hash.data[0];
        roothash.data[1] ^= hash.data[1];
        roothash.data[2] ^= hash.data[2];
        roothash.data[3] ^= hash.data[3];
    }
    hashes[0] = roothash;

    // Write the updated hash list into the block hash map file.
    pwrite(hmapfd, &hashes, newhmap_filesize, 0);
    ftruncate(hmapfd, newhmap_filesize);

    return 0;
}

int hashmap_builder::get_updatedhashes(
    hasher::B2H *hashes, std::string_view relpath, const bool oldhmap_exists, const int hmapfd, const int orifd,
    const uint32_t blockcount, const std::map<uint32_t, hasher::B2H> bindex, const off_t newhmap_filesize)
{
    // Load up old hashes from the hashmap file if the block index exists.
    // This will allow us to only update the new hashes using the block index.
    if (!bindex.empty() && oldhmap_exists && pread(hmapfd, &hashes, newhmap_filesize, 0) == -1)
    {
        std::cerr << "Read failed on block hash map for " << relpath << '\n';
        return -1;
    }

    for (uint32_t blockid = 0; blockid < blockcount; blockid++)
    {
        // We already have a hash loaded from hash map file (if it exists).
        bool hashfound = oldhmap_exists;

        // Retrieve uptodate hash from the block index if any.
        const auto itr = bindex.find(blockid);
        if (itr != bindex.end())
        {
            hashes[blockid + 1] = itr->second;
            hashfound = true;
        }

        // If all above attempts fail, compute the hash using raw data block.
        if (!hashfound)
        {
            char block[BLOCK_SIZE];
            off_t blockoffset = BLOCK_SIZE * blockid;
            if (pread(orifd, block, BLOCK_SIZE, blockoffset) == -1)
            {
                std::cerr << "Read failed " << relpath << '\n';
                return -1;
            }

            hashes[blockid + 1] = hasher::hash(&blockoffset, 8, block, BLOCK_SIZE);
        }
    }

    return 0;
}

int hashmap_builder::open_blockhashmap(int &hmapfd, bool &oldhmap_exists, std::string_view relpath)
{
    std::string hmapfile;
    hmapfile.reserve(hashmapdir.length() + relpath.length() + EXT_LEN);
    hmapfile.append(hashmapdir).append(relpath).append(HASHMAP_EXT);

    oldhmap_exists = boost::filesystem::exists(hmapfile);

    if (!oldhmap_exists)
    {
        // Create directory tree if not exist so we are able to create the hashmap files.
        boost::filesystem::path hmapsubdir = boost::filesystem::path(hmapfile).parent_path();
        if (created_hmapsubdirs.count(hmapsubdir.string()) == 0)
        {
            boost::filesystem::create_directories(hmapsubdir);
            created_hmapsubdirs.emplace(hmapsubdir.string());
        }
    }

    hmapfd = open(hmapfile.data(), O_RDWR | O_CREAT, 0644);
    if (hmapfd == -1)
    {
        std::cerr << "Open failed " << hmapfile << '\n';
        return -1;
    }

    return 0;
}

int hashmap_builder::get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, uint32_t &blockcount, std::string_view filerelpath)
{
    std::string bindexfile;
    bindexfile.reserve(cachedir.length() + filerelpath.length() + EXT_LEN);
    bindexfile.append(cachedir).append(filerelpath).append(BLOCKINDEX_EXT);

    if (boost::filesystem::exists(bindexfile))
    {
        std::ifstream infile(bindexfile, std::ios::binary | std::ios::ate);
        std::streamsize idxsize = infile.tellg();
        infile.seekg(0, std::ios::beg);

        // Read the block index file into a vector.
        std::vector<char> bindex(idxsize);
        if (infile.read(bindex.data(), idxsize))
        {
            // First 8 bytes contain the original file length.
            off_t orifilelength = 0;
            memcpy(&orifilelength, bindex.data(), 8);
            blockcount = ceil((double)orifilelength / (double)BLOCK_SIZE);

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
            return -1;
        }

        infile.close();
    }

    return 0;
}

} // namespace statehashmap

int main(int argc, char *argv[])
{
    if (argc != 4)
        exit(1);

    statehashmap::hashmap_builder builder(
        realpath(argv[1], NULL),
        realpath(argv[2], NULL),
        realpath(argv[3], NULL));
    builder.generate(std::list<std::string>());

    std::cout << "Done.\n";
}