#ifndef _STATEFS_HASHMAP_BUILDER_
#define _STATEFS_HASHMAP_BUILDER_

#include <string>
#include <list>
#include <map>
#include <vector>
#include <unordered_set>
#include "hasher.hpp"
#include "state_common.hpp"

namespace statefs
{

class hashmap_builder
{
private:
    const statedirctx &ctx;
    // List of new block hash map sub directories created during the session.
    std::unordered_set<std::string> created_bhmapsubdirs;

    int open_blockhashmap(int &hmapfd, bool &oldhmap_exists, std::string &hmapfile, const std::string &relpath);
    int get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, uint32_t &blockcount, const std::string &filerelpath);
    int get_updatedhashes(
        hasher::B2H *hashes, const std::string &relpath, const bool oldhmap_exists, const int hmapfd, const int orifd,
        const uint32_t blockcount, const std::map<uint32_t, hasher::B2H> bindex, const off_t newhashmap_filesize);
    int update_hashtree_entry(hasher::B2H &parentdirhash, const bool oldbhmap_exists, const hasher::B2H oldfilehash, const hasher::B2H newfilehash, const std::string &bhmapfile, const std::string &relpath);

public:
    hashmap_builder(const statedirctx &ctx);
    int generate_hashmap_forfile(hasher::B2H &parentdirhash, const std::string &filepath);
    int remove_hashmapfile(hasher::B2H &parentdirhash, const std::string &filepath);
};

} // namespace statefs

#endif
