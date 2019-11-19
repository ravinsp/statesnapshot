#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <boost/filesystem.hpp>
#include "hashtree_builder.hpp"
#include "state_common.hpp"

namespace statefs
{

hashtree_builder::hashtree_builder(std::string statedir, std::string changesetdir, std::string blockhashmapdir, std::string hashtreedir) : statedir(statedir),
                                                                                                                                           changesetdir(changesetdir),
                                                                                                                                           blockhashmapdir(blockhashmapdir),
                                                                                                                                           hashtreedir(hashtreedir),
                                                                                                                                           hmapbuilder(hashmap_builder(statedir, changesetdir, blockhashmapdir, hashtreedir))
{
}

int hashtree_builder::generate()
{
    if (!boost::filesystem::exists(statedir) ||
        !boost::filesystem::exists(changesetdir) ||
        !boost::filesystem::exists(blockhashmapdir) ||
        !boost::filesystem::exists(hashtreedir))
    {
        std::cerr << "Specified directories does not exist.";
        return -1;
    }

    // Load modified file path hints if available.
    populate_hintpaths(IDX_TOUCHEDFILES);
    populate_hintpaths(IDX_NEWFILES);

    hasher::B2H dummyhash{0, 0, 0, 0};
    if (update_hashtree_fordir(dummyhash, statedir) == -1)
        return -1;

    return 0;
}

int hashtree_builder::update_hashtree_fordir(hasher::B2H &parentdirhash, const std::string &dirpath)
{
    const std::string &relpath = dirpath.substr(statedir.length(), dirpath.length() - statedir.length());
    const std::string htreedirpath = hashtreedir + relpath;
    const std::string dirhashfile = htreedirpath + "/" + DIRHASH_FNAME;

    // Load current dir hash if exist.
    hasher::B2H dirhash{0, 0, 0, 0};
    int dirhashfd = open(dirhashfile.c_str(), O_RDONLY);
    if (dirhashfd > 0)
    {
        read(dirhashfd, &dirhash, hasher::HASH_SIZE);
        close(dirhashfd);
    }

    hasher::B2H original_dirhash = dirhash;

    const boost::filesystem::directory_iterator itrend;
    for (boost::filesystem::directory_iterator itr(dirpath); itr != itrend; itr++)
    {
        const bool isdir = boost::filesystem::is_directory(itr->path());
        const std::string pathstr = itr->path().string();

        // Skip if not in hinted paths.
        if (!is_hinted_path(pathstr, isdir))
            continue;

        if (isdir)
        {
            if (update_hashtree_fordir(dirhash, pathstr) == -1)
                return -1;
        }
        else
        {
            // Create directory tree if not exist so we are able to create the root hash map files.
            if (created_htreesubdirs.count(htreedirpath) == 0)
            {
                boost::filesystem::create_directories(htreedirpath);
                created_htreesubdirs.emplace(htreedirpath);
            }

            if (hmapbuilder.generate_hashmap_forfile(dirhash, pathstr) == -1)
                return -1;
        }
    }

    if (dirhash != original_dirhash)
    {
        // If dir hash has changed, write it back to dir hash file.
        dirhashfd = open(dirhashfile.c_str(), O_RDWR | O_TRUNC | O_CREAT, FILE_PERMS);
        if (dirhashfd == -1)
            return -1;
        if (write(dirhashfd, &dirhash, hasher::HASH_SIZE) == -1)
        {
            close(dirhashfd);
            return -1;
        }
        close(dirhashfd);

        // Also update the parent dir hash by subtracting the old hash and adding the new hash.
        parentdirhash ^= original_dirhash;
        parentdirhash ^= dirhash;
    }

    return 0;
}

bool hashtree_builder::is_hinted_path(const std::string &path, const bool isdir)
{
    // If there are no hints, consider all dirs and files.
    if (filepathhints.empty())
        return true;

    if (isdir)
    {
        for (const std::string hintpath : filepathhints)
        {
            if (strncmp(path.c_str(), hintpath.c_str(), path.length()) == 0)
                return true;
        }
        return false;
    }
    else
    {
        return filepathhints.count(path) > 0;
    }
}

void hashtree_builder::populate_hintpaths(const char *const idxfile)
{
    std::ifstream infile(std::string(changesetdir).append(IDX_TOUCHEDFILES));
    if (!infile.fail())
    {
        for (std::string relpath; std::getline(infile, relpath);)
            filepathhints.emplace(std::string(statedir + relpath));
        infile.close();
    }
}

} // namespace statefs

int main(int argc, char *argv[])
{
    if (argc == 5)
    {
        const char *statedir = realpath(argv[1], NULL);
        const char *changesetdir = realpath(argv[2], NULL);
        const char *blockhashmapdir = realpath(argv[3], NULL);
        const char *hashtreedir = realpath(argv[4], NULL);

        statefs::hashtree_builder builder(statedir, changesetdir, blockhashmapdir, hashtreedir);
        if (builder.generate() == -1)
            std::cerr << "Generation failed\n";
        else
            std::cout << "Done.\n";
    }
    else if (argc == 2)
    {
        // Print the hashes in bhmap file.
        const char *hmapfile = realpath(argv[1], NULL);
        hasher::B2H hash[4];
        int fd = open(hmapfile, O_RDONLY);
        int res = read(fd, hash, 128);

        for (int i = 0; i < 4; i++)
            std::cout << std::hex << hash[i].data[0] << hash[i].data[1] << hash[i].data[2] << hash[i].data[3] << "\n";

        std::cout << "Done.\n";
    }
    else
    {
        std::cerr << "Incorrect arguments.\n";
        exit(1);
    }
}