#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <boost/filesystem.hpp>
#include "hashtree_builder.hpp"
#include "state_common.hpp"

namespace statefs
{

hashtree_builder::hashtree_builder(std::string statedir, std::string changesetdir, std::string blockhashmapdir, std::string hashtreedir)
    : statedir(statedir),
      changesetdir(changesetdir),
      blockhashmapdir(blockhashmapdir),
      hashtreedir(hashtreedir),
      hmapbuilder(hashmap_builder(statedir, changesetdir, blockhashmapdir, hashtreedir))
{
}

int hashtree_builder::generate()
{
    // Load modified file path hints if available.
    populate_hintpaths(IDX_TOUCHEDFILES);
    populate_hintpaths(IDX_NEWFILES);
    hintmode = !hintpaths.empty();

    traversel_rootdir = statedir;
    removal_mode = false;
    update_hashtree();

    for (const auto &[d, t] : hintpaths)
        std::cout << d << " remaining\n";

    // If there are any remaining hint files directly under this directory, that means
    // those files are no longer there. So we need to delete the corresponding .bhmap and rh files
    // and adjust the directory hash accordingly.
    if (hintmode && !hintpaths.empty())
    {
        traversel_rootdir = blockhashmapdir;
        removal_mode = true;
        update_hashtree();
    }

    return 0;
}

int hashtree_builder::update_hashtree()
{
    hintpath_map::iterator hintdir_itr;
    if (!should_process_dir(hintdir_itr, "/"))
        return 0;

    hasher::B2H emptyhash{0, 0, 0, 0};
    if (update_hashtree_fordir(emptyhash, traversel_rootdir, hintdir_itr) == -1)
        return -1;

    return 0;
}

int hashtree_builder::update_hashtree_fordir(hasher::B2H &parentdirhash, const std::string &dirpath, const hintpath_map::iterator hintdir_itr)
{
    std::cout << "Visited: " << dirpath << " Removal mode:" << removal_mode << "\n";

    const std::string &relpath = dirpath.substr(traversel_rootdir.length(), dirpath.length() - traversel_rootdir.length());
    const std::string htreedirpath = hashtreedir + relpath;

    // Load current dir hash if exist.
    const std::string dirhashfile = htreedirpath + "/" + DIRHASH_FNAME;
    hasher::B2H dirhash = get_existingdirhash(dirhashfile);

    // Remember the dir hash before we mutate it.
    hasher::B2H original_dirhash = dirhash;

    // Iterate files/subdirs inside this dir.
    const boost::filesystem::directory_iterator itrend;
    for (boost::filesystem::directory_iterator itr(dirpath); itr != itrend; itr++)
    {
        const bool isdir = boost::filesystem::is_directory(itr->path());
        const std::string pathstr = itr->path().string();

        if (isdir)
        {
            hintpath_map::iterator hintsubdir_itr;
            if (!should_process_dir(hintsubdir_itr, pathstr))
                continue;

            if (update_hashtree_fordir(dirhash, pathstr, hintsubdir_itr) == -1)
                return -1;
        }
        else
        {
            if (!should_process_file(pathstr, hintdir_itr))
                continue;

            if (process_file(dirhash, pathstr, htreedirpath) == -1)
                return -1;
        }
    }

    if (dirhash != original_dirhash)
    {
        // If dir hash has changed, write it back to dir hash file.
        if (save_dirhash(dirhashfile, dirhash) == -1)
            return -1;

        // Also update the parent dir hash by subtracting the old hash and adding the new hash.
        parentdirhash ^= original_dirhash;
        parentdirhash ^= dirhash;
    }

    return 0;
}

hasher::B2H hashtree_builder::get_existingdirhash(const std::string &dirhashfile)
{
    // Load current dir hash if exist.
    hasher::B2H dirhash{0, 0, 0, 0};
    int dirhashfd = open(dirhashfile.c_str(), O_RDONLY);
    if (dirhashfd > 0)
    {
        read(dirhashfd, &dirhash, hasher::HASH_SIZE);
        close(dirhashfd);
    }
    return dirhash;
}

int hashtree_builder::save_dirhash(const std::string &dirhashfile, hasher::B2H dirhash)
{
    int dirhashfd = open(dirhashfile.c_str(), O_RDWR | O_TRUNC | O_CREAT, FILE_PERMS);
    if (dirhashfd == -1)
        return -1;
    if (write(dirhashfd, &dirhash, hasher::HASH_SIZE) == -1)
    {
        close(dirhashfd);
        return -1;
    }
    close(dirhashfd);
    return 0;
}

inline bool hashtree_builder::should_process_dir(hintpath_map::iterator &dir_itr, const std::string &dirpath)
{
    return hintmode ? get_hinteddir_match(dir_itr, dirpath) : true;
}

bool hashtree_builder::should_process_file(const std::string filepath, const hintpath_map::iterator hintdir_itr)
{
    if (hintmode)
    {
        if (hintdir_itr == hintpaths.end())
            return false;

        std::unordered_set<std::string> &hintfiles = hintdir_itr->second;
        const auto hintfile_itr = hintfiles.find(filepath);
        if (hintfile_itr == hintfiles.end())
            return false;

        // Erase the visiting filepath from hint files.
        hintfiles.erase(hintfile_itr);

        // If there are no more files in the hint dir, delete the hin dir entry as well.
        if (hintfiles.empty())
            hintpaths.erase(hintdir_itr);
    }
    return true;
}

int hashtree_builder::process_file(hasher::B2H &parentdirhash, const std::string &filepath, const std::string &htreedirpath)
{
    if (!removal_mode)
    {
        // Create directory tree if not exist so we are able to create the root hash map files.
        if (created_htreesubdirs.count(htreedirpath) == 0)
        {
            boost::filesystem::create_directories(htreedirpath);
            created_htreesubdirs.emplace(htreedirpath);
        }

        if (hmapbuilder.generate_hashmap_forfile(parentdirhash, filepath) == -1)
            return -1;
    }
    else
    {
        if (hmapbuilder.remove_hashmapfile(parentdirhash, filepath) == -1)
            return -1;
    }

    return 0;
}

void hashtree_builder::populate_hintpaths(const char *const idxfile)
{
    std::ifstream infile(std::string(changesetdir).append(idxfile));
    if (!infile.fail())
    {
        for (std::string relpath; std::getline(infile, relpath);)
        {
            std::string filepath = statedir + relpath;
            std::string parentdir = boost::filesystem::path(filepath).parent_path().string();
            hintpaths[parentdir].emplace(filepath);
        }
        infile.close();
    }
}

bool hashtree_builder::get_hinteddir_match(hintpath_map::iterator &itr, const std::string &dirpath)
{
    // First check whether there's an exact match. If not check for a partial match.
    // Exact match will return the iterator. Partial match or not found will return end() iterator.

    const auto exactmatchitr = hintpaths.find(dirpath);
    if (exactmatchitr != hintpaths.end())
    {
        itr = exactmatchitr;
        return true;
    }

    for (auto itr = hintpaths.begin(); itr != hintpaths.end(); itr++)
    {
        if (strncmp(dirpath.c_str(), itr->first.c_str(), dirpath.length()) == 0)
        {
            // Partial match found.
            itr = hintpaths.end();
            return true;
        }
    }

    return false; // Not found at all.
}

} // namespace statefs

int main(int argc, char *argv[])
{
    if (argc == 4 || argc == 5)
    {
        const char *statedir = realpath(argv[1], NULL);
        const char *blockhashmapdir = realpath(argv[2], NULL);
        const char *hashtreedir = realpath(argv[3], NULL);

        const char *changesetdir =
            (argc == 5 && boost::filesystem::exists(argv[4])) ? realpath(argv[4], NULL) : "";

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