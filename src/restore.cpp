#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace staterestore
{
const char *const IDX_NEWFILES = "/idxnew.idx";
const char *const IDX_TOUCHEDFILES = "/idxtouched.idx";
const char *const BLOCKCACHE_EXT = ".bcache";
const char *const BLOCKINDEX_EXT = ".bindex";
constexpr uint8_t BLOCKINDEX_ENTRY_SIZE = 44;
constexpr size_t BLOCK_SIZE = 4 * 1024; //* 1024; // 4MB

// Look at new files added and delete them if still exist.
void delete_newfiles(const char *statedir, const char *chkpntdir)
{
    std::string indexfile(chkpntdir);
    indexfile.append(IDX_NEWFILES);

    std::ifstream infile(indexfile);
    for (std::string file; std::getline(infile, file);)
    {
        std::string filepath(statedir);
        filepath.append(file);

        std::remove(filepath.c_str());
    }

    infile.close();
}

int read_blockindex(std::vector<char> &buffer, std::string_view file, const char *statedir, const char *chkpntdir)
{
    std::string bindexfile(chkpntdir);
    bindexfile.append(file).append(BLOCKINDEX_EXT);
    std::ifstream infile(bindexfile, std::ios::binary | std::ios::ate);
    std::streamsize size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    buffer.resize(size);
    if (!infile.read(buffer.data(), size))
    {
        std::cout << "Failed to read " << bindexfile << "\n";
        return -1;
    }

    return 0;
}

int restore_blocks(std::string_view file, const std::vector<char> &bindex, const char *statedir, const char *chkpntdir)
{
    int bcachefd = 0, orifilefd = 0;
    const char *idxptr = bindex.data();

    // First 8 bytes of the index contains the supposed length of the original file.
    off_t originallen = 0;
    memcpy(&originallen, idxptr, 8);

    // Open block cache file.
    {
        std::string bcachefile(chkpntdir);
        bcachefile.append(file).append(BLOCKCACHE_EXT);
        bcachefd = open(bcachefile.c_str(), O_RDONLY);
        if (bcachefd <= 0)
        {
            std::cout << "Error opening " << bcachefile << "\n";
            return -1;
        }
    }

    // Open original file.
    {
        std::string originalfile(statedir);
        originalfile.append(file);

        orifilefd = open(originalfile.c_str(), O_RDONLY | O_CREAT, 0644);
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

        std::cout << "oo:" << orifileoffset << " bco:" << bcacheoffset << "\n";

        char buf[32];
        lseek(bcachefd, bcacheoffset, SEEK_SET);
        read(bcachefd, buf, 32);
        std::cout << buf << "\n";

        // Transfer the cached block to the target file.
        //copy_file_range(bcachefd, &bcacheoffset, orifilefd, &orifileoffset, BLOCK_SIZE, 0);
    }

    // If the target file is bigger than the original size, truncate it to the original size.
    // ftruncate(orifilefd, originallen);

    close(bcachefd);
    close(orifilefd);

    return 0;
}

int restore(const char *statedir, const char *chkpntdir)
{
    delete_newfiles(statedir, chkpntdir);

    // Look at touched files and restore them.
    {
        std::unordered_set<std::string> processed;

        std::string indexfile(chkpntdir);
        indexfile.append(IDX_TOUCHEDFILES);

        std::ifstream infile(indexfile);
        for (std::string file; std::getline(infile, file);)
        {
            // Skip if already processed.
            if (processed.count(file) > 0)
                continue;

            std::vector<char> bindex;
            if (read_blockindex(bindex, file, statedir, chkpntdir) != 0)
                return -1;

            if (restore_blocks(file, bindex, statedir, chkpntdir) != 0)
                return -1;

            // Add to processed file list.
            processed.emplace(file);
        }

        infile.close();
    }
}

} // namespace staterestore

int main(int argc, char *argv[])
{
    if (argc != 3)
        exit(1);

    staterestore::restore(
        realpath(argv[1], NULL),
        realpath(argv[2], NULL));

    std::cout << "Done.\n";
}