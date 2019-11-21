#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <unordered_set>
#include <vector>
#include <boost/filesystem.hpp>
#include "state_restore.hpp"
#include "hashtree_builder.hpp"
#include "state_common.hpp"

namespace statefs
{

// Look at new files added and delete them if still exist.
void state_restore::delete_newfiles()
{
    std::string indexfile(ctx.changesetdir);
    indexfile.append(IDX_NEWFILES);

    std::ifstream infile(indexfile);
    for (std::string file; std::getline(infile, file);)
    {
        std::string filepath(ctx.datadir);
        filepath.append(file);

        std::remove(filepath.c_str());
    }

    infile.close();
}

// Look at touched files and restore them.
int state_restore::restore_touchedfiles()
{
    std::unordered_set<std::string> processed;

    std::string indexfile(ctx.changesetdir);
    indexfile.append(IDX_TOUCHEDFILES);

    std::ifstream infile(indexfile);
    for (std::string file; std::getline(infile, file);)
    {
        // Skip if already processed.
        if (processed.count(file) > 0)
            continue;

        std::vector<char> bindex;
        if (read_blockindex(bindex, file) != 0)
            return -1;

        if (restore_blocks(file, bindex) != 0)
            return -1;

        // Add to processed file list.
        processed.emplace(file);
    }

    infile.close();
    return 0;
}

int state_restore::read_blockindex(std::vector<char> &buffer, std::string_view file)
{
    std::string bindexfile(ctx.changesetdir);
    bindexfile.append(file).append(BLOCKINDEX_EXT);
    std::ifstream infile(bindexfile, std::ios::binary | std::ios::ate);
    std::streamsize idxsize = infile.tellg();
    infile.seekg(0, std::ios::beg);

    buffer.resize(idxsize);
    if (!infile.read(buffer.data(), idxsize))
    {
        std::cout << "Failed to read " << bindexfile << "\n";
        return -1;
    }

    return 0;
}

int state_restore::restore_blocks(std::string_view file, const std::vector<char> &bindex)
{
    int bcachefd = 0, orifilefd = 0;
    const char *idxptr = bindex.data();

    // First 8 bytes of the index contains the supposed length of the original file.
    off_t originallen = 0;
    memcpy(&originallen, idxptr, 8);

    // Open block cache file.
    {
        std::string bcachefile(ctx.changesetdir);
        bcachefile.append(file).append(BLOCKCACHE_EXT);
        bcachefd = open(bcachefile.c_str(), O_RDONLY);
        if (bcachefd <= 0)
        {
            std::cout << "Error opening " << bcachefile << "\n";
            return -1;
        }
    }

    // Create or Open original file.
    {
        std::string originalfile(ctx.datadir);
        originalfile.append(file);

        // Create directory tree if not exist so we are able to create the file.
        boost::filesystem::path filedir = boost::filesystem::path(originalfile).parent_path();
        if (created_dirs.count(filedir.string()) == 0)
        {
            boost::filesystem::create_directories(filedir);
            created_dirs.emplace(filedir.string());
        }

        orifilefd = open(originalfile.c_str(), O_WRONLY | O_CREAT, FILE_PERMS);
        if (orifilefd <= 0)
        {
            std::cout << "Error opening " << originalfile << "\n";
            return -1;
        }
    }

    // Restore the blocks as specified in block index.
    for (uint32_t idxoffset = 8; idxoffset < bindex.size();)
    {
        // Find the block no. of where this block is from in the original file.
        uint32_t blockno = 0;
        memcpy(&blockno, idxptr + idxoffset, 4);
        idxoffset += 4;
        off_t orifileoffset = blockno * BLOCK_SIZE;

        // Find the offset where the block is located in the block cache file.
        off_t bcacheoffset;
        memcpy(&bcacheoffset, idxptr + idxoffset, 8);
        idxoffset += 40; // Skip the hash(32)

        // Transfer the cached block to the target file.
        copy_file_range(bcachefd, &bcacheoffset, orifilefd, &orifileoffset, BLOCK_SIZE, 0);
    }

    // If the target file is bigger than the original size, truncate it to the original size.
    off_t currentlen = lseek(orifilefd, 0, SEEK_END);
    if (currentlen > originallen)
        ftruncate(orifilefd, originallen);

    close(bcachefd);
    close(orifilefd);

    return 0;
}

int state_restore::rollback()
{
    ctx = get_statedir_context();

    delete_newfiles();
    if (restore_touchedfiles() == -1)
        return -1;

    // Update hash tree.
    hashtree_builder htreebuilder(ctx);
    htreebuilder.generate();

    return 0;
}

} // namespace statefs