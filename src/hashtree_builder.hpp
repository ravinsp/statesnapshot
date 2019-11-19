#ifndef _STATEFS_HASHTREE_BUILDER_
#define _STATEFS_HASHTREE_BUILDER_

#include <unordered_set>
#include "hasher.hpp"
#include "hashmap_builder.hpp"

namespace statefs
{

class hashtree_builder
{
private:
    const std::string statedir, changesetdir, blockhashmapdir, hashtreedir;
    hashmap_builder hmapbuilder;

    std::unordered_set<std::string> filepathhints;
    // List of new root hash map sub directories created during the session.
    std::unordered_set<std::string> created_htreesubdirs;

    int update_hashtree_fordir(hasher::B2H &parentdirhash, const std::string &relpath);
    int update_hashtree_entry(hasher::B2H &parentdirhash, const bool oldbhmap_exists, const hasher::B2H oldfilehash, const hasher::B2H newfilehash, const std::string &bhmapfile, const std::string &relpath);
    void populate_hintpaths(const char *const idxfile);
    bool is_hinted_path(const std::string &path, const bool isdir);

public:
    hashtree_builder(std::string statedir, std::string changesetdir, std::string blockhashmapdir, std::string hashtreedir);
    int generate();
};

} // namespace statefs

#endif
