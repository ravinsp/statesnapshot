#ifndef _HASHER_
#define _HASHER_

#include <sodium.h>

namespace hasher
{

constexpr size_t HASH_SIZE = crypto_generichash_blake2b_BYTES;

struct B2H // blake2b hash is 32 bytes which we store as 4 quad words
{
    uint64_t data[4];
};

// provide some helper functions for working with 32 byte hash type
bool operator==(B2H &lhs, B2H &rhs)
{
    return lhs.data[0] == rhs.data[0] && lhs.data[1] == rhs.data[1] && lhs.data[2] == rhs.data[2] && lhs.data[3] == rhs.data[3];
}

// the actual hash function, note that the B2H datatype is always passed by value being only 4 quadwords
B2H hash(const void *ptr, size_t len)
{
    B2H ret;
    crypto_generichash_blake2b_state state;
    crypto_generichash_blake2b_init(&state, NULL, 0, HASH_SIZE);
    crypto_generichash_blake2b_update(&state,
                                      reinterpret_cast<const unsigned char *>(ptr), len);
    crypto_generichash_blake2b_final(
        &state,
        reinterpret_cast<unsigned char *>(&ret),
        HASH_SIZE);
    return ret;
}

} // namespace hasher

#endif