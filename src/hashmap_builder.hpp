#ifndef _HASHMAP_BUILDER_
#define _HASHMAP_BUILDER_

#include <string>
#include <list>
#include <map>
#include <vector>
#include <unordered_set>
#include "hasher.hpp"

namespace statehashmap
{

class hashmap_builder
{
private:
    std::string statedir, changesetdir, blockhashmapdir, roothashmapdir;
    int generate_filehashmaps();
    int generate_dirhashes();
    void populate_paths_toset(std::unordered_set<std::string> &lines, const std::string &filepath);
    int generate_hashmap_forfile(std::string_view filepath);
    int get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, uint32_t &blockcount, std::string_view filerelpath);
    int open_blockhashmap(int &hmapfd, bool &oldhmap_exists, std::string &hmapfile, std::string_view relpath);
    int get_updatedhashes(
        hasher::B2H *hashes, std::string_view relpath, const bool oldhmap_exists, const int hmapfd, const int orifd,
        const uint32_t blockcount, const std::map<uint32_t, hasher::B2H> bindex, const off_t newhashmap_filesize);

    // List of new hashmap sub directories created during the session.
    std::unordered_set<std::string> created_bhmapsubdirs;

public:
    hashmap_builder(std::string statedir, std::string changesetdir, std::string blockhashmapdir, std::string roothashmapdir);
    int generate();
};

} // namespace statehashmap

#endif
