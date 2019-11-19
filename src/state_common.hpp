#ifndef _STATEFS_STATE_COMMON_
#define _STATEFS_STATE_COMMON_

#include <sys/types.h>
#include "hasher.hpp"

namespace statefs
{

// Cache block size.
constexpr size_t BLOCK_SIZE = 4 * 1024; //* 1024; // 4MB

// Cache block index entry bytes length.
constexpr size_t BLOCKINDEX_ENTRY_SIZE = 44;
constexpr size_t MAX_HASHES = BLOCK_SIZE / hasher::HASH_SIZE;

// Permissions used when creating block cache and index files.
constexpr int FILE_PERMS = 0644;

const char *const HASHMAP_EXT = ".bhmap";
constexpr size_t HASHMAP_EXT_LEN = 6;

const char *const BLOCKINDEX_EXT = ".bindex";
constexpr size_t BLOCKINDEX_EXT_LEN = 7;

const char *const BLOCKCACHE_EXT = ".bcache";
constexpr size_t BLOCKCACHE_EXT_LEN = 7;

const char *const IDX_NEWFILES = "/idxnew.idx";
const char *const IDX_TOUCHEDFILES = "/idxtouched.idx";
const char *const DIRHASH_FNAME = "dir.hash";

} // namespace statefs

#endif