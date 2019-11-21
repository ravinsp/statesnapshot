#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <boost/filesystem.hpp>
#include "state_common.hpp"
#include "hashmap_builder.hpp"
#include "hasher.hpp"

namespace statefs
{

hashmap_builder::hashmap_builder(const statedirctx &ctx) : ctx(ctx)
{
}

int hashmap_builder::generate_hashmap_forfile(hasher::B2H &parentdirhash, const std::string &filepath)
{
    // We attempt to avoid a full rebuild of the block hash map file when possible.
    // For this optimisation, both the block hash map (.bhmap) file and the
    // block cache index (.bindex) files must exist.

    // If the block index exists, we generate/update the hashmap file with the aid of that.
    // Block index file contains the total length of original file and updated block hashes.
    // If not, we simply read the original file and recalculate all the block hashes.

    std::string relpath = get_relpath(filepath, ctx.datadir);

    uint32_t blockcount = 0;
    int orifd = 0, hmapfd = 0;
    bool oldbhmap_exists = false;

    std::string bhmapfile;
    if (open_blockhashmap(hmapfd, oldbhmap_exists, bhmapfile, relpath) == -1)
        return -1;

    // Attempt to read the block index file.
    std::map<uint32_t, hasher::B2H> bindex;
    if (get_blockindex(bindex, blockcount, relpath) == -1)
        return -1;

    // No need to read original file if the block index has all the blocks information.
    // If block index is not sufficient, we'll need to read raw data blocks from the original file.
    if (bindex.empty() || bindex.size() < blockcount)
    {
        orifd = open(filepath.data(), O_RDONLY);
        if (orifd == -1)
        {
            std::cerr << errno << ": Open failed " << filepath << '\n';
            return -1;
        }

        // Detect the block count if not already loaded by block index file.
        if (blockcount == 0)
        {
            const off_t orifilelength = lseek(orifd, 0, SEEK_END);
            blockcount = ceil((double)orifilelength / (double)BLOCK_SIZE);
        }
    }

    // Build up the latest block hash list in memory.
    // Hashes maybe fetched from block index or existing block hash map (if available) or
    // recalculated from original file.
    hasher::B2H hashes[1 + blockcount]; // slot 0 is for the root hash.
    const size_t newhmap_filesize = (1 + blockcount) * hasher::HASH_SIZE;
    if (get_updatedhashes(
            hashes, relpath, oldbhmap_exists, hmapfd, orifd,
            blockcount, bindex, newhmap_filesize) == -1)
        return -1;

    // Calculate the new file hash: filehash = HASH(filename + XOR(block hashes))
    hasher::B2H filehash{0, 0, 0, 0};
    for (int i = 1; i < blockcount; i++)
        filehash ^= hashes[i];

    // Rehash the file hash with filename included.
    const std::string filename = boost::filesystem::path(relpath.data()).filename().string();
    filehash = hasher::hash(filename.c_str(), filename.length(), &filehash, hasher::HASH_SIZE);

    // Get the old file hash before we assign the new root hash.
    hasher::B2H oldfilehash = hashes[0];
    hashes[0] = filehash;

    // Write the updated hash list into the block hash map file.
    if (pwrite(hmapfd, &hashes, newhmap_filesize, 0) == -1)
        return -1;
    if (ftruncate(hmapfd, newhmap_filesize) == -1)
        return -1;

    if (update_hashtree_entry(parentdirhash, oldbhmap_exists, oldfilehash, filehash, bhmapfile, relpath) == -1)
        return -1;

    return 0;
}

int hashmap_builder::open_blockhashmap(int &hmapfd, bool &oldbhmap_exists, std::string &bhmapfile, const std::string &relpath)
{
    bhmapfile.reserve(ctx.blockhashmapdir.length() + relpath.length() + HASHMAP_EXT_LEN);
    bhmapfile.append(ctx.blockhashmapdir).append(relpath).append(HASHMAP_EXT);

    oldbhmap_exists = boost::filesystem::exists(bhmapfile);

    if (!oldbhmap_exists)
    {
        // Create directory tree if not exist so we are able to create the hashmap files.
        boost::filesystem::path hmapsubdir = boost::filesystem::path(bhmapfile).parent_path();
        if (created_bhmapsubdirs.count(hmapsubdir.string()) == 0)
        {
            boost::filesystem::create_directories(hmapsubdir);
            created_bhmapsubdirs.emplace(hmapsubdir.string());
        }
    }

    hmapfd = open(bhmapfile.data(), O_RDWR | O_CREAT, FILE_PERMS);
    if (hmapfd == -1)
    {
        std::cerr << errno << ": Open failed " << bhmapfile << '\n';
        return -1;
    }

    return 0;
}

int hashmap_builder::get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, uint32_t &blockcount, const std::string &filerelpath)
{
    std::string bindexfile;
    bindexfile.reserve(ctx.changesetdir.length() + filerelpath.length() + BLOCKINDEX_EXT_LEN);
    bindexfile.append(ctx.changesetdir).append(filerelpath).append(BLOCKINDEX_EXT);

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
                idxoffset += 12; // Skip the cached block offset (8 bytes)

                // Read the block hash (32 bytes).
                hasher::B2H hash;
                memcpy(&hash, bindex.data() + idxoffset, 32);
                idxoffset += 32;

                idxmap.try_emplace(blockno, hash);
            }
        }
        else
        {
            std::cerr << errno << ": Read failed " << bindexfile << '\n';
            return -1;
        }

        infile.close();
    }

    return 0;
}

int hashmap_builder::get_updatedhashes(
    hasher::B2H *hashes, const std::string &relpath, const bool oldhmap_exists, const int hmapfd, const int orifd,
    const uint32_t blockcount, const std::map<uint32_t, hasher::B2H> bindex, const off_t newhmap_filesize)
{
    // Load up old hashes from the hashmap file if the block index exists.
    // This will allow us to update the new hashes only using the block index.
    const bool loadhashes_frombhmap = !bindex.empty() && oldhmap_exists;

    if (oldhmap_exists)
    {
        // If we are not loading all hashes from the .bhmap, just load the root hash from it.
        const off_t readlen = loadhashes_frombhmap ? newhmap_filesize : hasher::HASH_SIZE;

        if (pread(hmapfd, hashes, readlen, 0) == -1)
        {
            std::cerr << errno << ": Read failed on block hash map for " << relpath << '\n';
            return -1;
        }
    }
    else
    {
        // Reset the root hash so we don't retain any uninitialized memory.
        hashes[0] = {0, 0, 0, 0};
    }

    for (uint32_t blockid = 0; blockid < blockcount; blockid++)
    {
        // We may already have a block hash loaded from block hash map file (if it exists).
        bool hashfound = loadhashes_frombhmap;

        // Retrieve more up-to-date hash from the block index if any.
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
            const off_t blockoffset = BLOCK_SIZE * blockid;
            if (pread(orifd, block, BLOCK_SIZE, blockoffset) == -1)
            {
                std::cerr << errno << ": Read failed " << relpath << '\n';
                return -1;
            }

            hashes[blockid + 1] = hasher::hash(&blockoffset, 8, block, BLOCK_SIZE);
        }
    }

    return 0;
}

int hashmap_builder::update_hashtree_entry(hasher::B2H &parentdirhash, const bool oldbhmap_exists, const hasher::B2H oldfilehash, const hasher::B2H newfilehash, const std::string &bhmapfile, const std::string &relpath)
{
    std::string hardlinkdir(ctx.hashtreedir);
    const std::string relpathdir = boost::filesystem::path(relpath).parent_path().string();

    hardlinkdir.append(relpathdir);
    if (relpathdir != "/")
        hardlinkdir.append("/");

    std::stringstream newhlpath;
    newhlpath << hardlinkdir << newfilehash << ".rh";

    if (oldbhmap_exists)
    {
        // Rename the existing hard link if old block hash map existed.
        // We thereby assume the old hard link also existed.
        std::stringstream oldhlpath;
        oldhlpath << hardlinkdir << oldfilehash << ".rh";
        if (rename(oldhlpath.str().c_str(), newhlpath.str().c_str()) == -1)
            return -1;

        // Subtract the old root hash and add the new root hash from the parent dir hash.
        parentdirhash ^= oldfilehash;
        parentdirhash ^= newfilehash;
    }
    else
    {
        // Create a new hard link with new root hash as the name.
        if (link(bhmapfile.c_str(), newhlpath.str().c_str()) == -1)
            return -1;

        // Add the new root hash to parent hash.
        parentdirhash ^= newfilehash;
    }

    return 0;
}

int hashmap_builder::remove_hashmapfile(hasher::B2H &parentdirhash, const std::string &bhmapfile)
{
    if (boost::filesystem::exists(bhmapfile))
    {
        int hmapfd = open(bhmapfile.data(), O_RDONLY);
        if (hmapfd == -1)
        {
            std::cerr << errno << ": Open failed " << bhmapfile << '\n';
            return -1;
        }

        hasher::B2H filehash;
        if (read(hmapfd, &filehash, hasher::HASH_SIZE) == -1)
        {
            std::cerr << errno << ": Read failed " << bhmapfile << '\n';
            return -1;
        }

        // Delete the .bhmap file.
        if (remove(bhmapfile.c_str()) == -1)
        {
            std::cerr << errno << ": Delete failed " << bhmapfile << '\n';
            return -1;
        }

        // Delete the hardlink of the .bhmap file.
        std::string hardlinkdir(ctx.hashtreedir);
        const std::string relpath = get_relpath(bhmapfile, ctx.blockhashmapdir);
        const std::string relpathdir = boost::filesystem::path(relpath).parent_path().string();

        hardlinkdir.append(relpathdir);
        if (relpathdir != "/")
            hardlinkdir.append("/");

        std::stringstream hlpath;
        hlpath << hardlinkdir << filehash << ".rh";
        if (remove(hlpath.str().c_str()) == -1)
        {
            std::cerr << errno << ": Delete failed for halrd link " << filehash << " of " << bhmapfile << '\n';
            return -1;
        }

        // XOR parent dir hash with file hash so the file hash gets removed from parent dir hash.
        parentdirhash ^= filehash;
    }

    return 0;
}

} // namespace statefs