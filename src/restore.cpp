#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace staterestore
{
const char *const IDX_NEWFILES = "/idxnew.idx";
const char *const IDX_TOUCHEDFILES = "/idxtouched.idx";
const char *const BLOCKCACHE_EXT = ".bcache";
const char *const BLOCKINDEX_EXT = ".bindex";

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
    std::string bcachefile(chkpntdir);
    bcachefile.append(file).append(BLOCKCACHE_EXT);

    std::string originalfile(statedir);
    originalfile.append(file);

    // First 8 bytes contain the supposed length of the original file.
    off_t originallen;
    memcpy(&originallen, bindex.data(), 8);

    // If the current file is bigger, truncate it to the original size.
    truncate(originalfile.c_str(), originallen);

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