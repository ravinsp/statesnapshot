#ifndef _HASHMAP_BUILDER_
#define _HASHMAP_BUILDER_

#include <string>
#include <list>
#include <map>
#include <vector>
#include "hasher.hpp"

class hashmap_builder
{
private:
    std::string statedir, cachedir, hashmapdir;

public:
    hashmap_builder(std::string statedir, std::string cachedir, std::string hashmapdir);
    int generate(std::list<std::string> filepathhints);
    int generate_hashmap_forfile(std::string_view filepath);
    int get_blockindex(std::map<uint32_t, hasher::B2H> &idxmap, off_t &orifilelength, std::string_view filerelpath);
    int insert_blockhash(const int hmapfd, const int blockid, void *hashbuf);
};

#endif
