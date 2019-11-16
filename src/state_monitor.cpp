#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unordered_map>
#include <cmath>
#include <boost/filesystem.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include "state_monitor.hpp"
#include "hasher.hpp"

namespace fusefs
{

constexpr size_t BLOCK_SIZE = 4 * 1024; //* 1024; // 4MB
constexpr size_t BLOCKINDEX_ENTRY_SIZE = 44;
constexpr size_t EXT_LEN = 8;
constexpr int FILE_PERMS = 0644;
const char *const BLOCKCACHE_EXT = ".bcache";
const char *const BLOCKINDEX_EXT = ".bindex";

void state_monitor::oncreate(const int fd)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getmappedpath_for_fd(filepath, fd) == 0)
    {
        state_file_info fi;
        fi.isnew = true;
        fi.filepath = filepath;
        fileinfomap[filepath] = std::move(fi);
        write_newfileentry(filepath);
    }
}

void state_monitor::onopen(const int inodefd, const int flags)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getpath_for_fd(filepath, inodefd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath) == 0)
        {
            // Check whether fd is open in truncate mode. If so cache the file immediately.
            if (flags & O_TRUNC)
                cache_blocks(*fi, 0, fi->original_length);
        }
    }
}

void state_monitor::onwrite(const int fd, const off_t offset, const size_t length)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getmappedpath_for_fd(filepath, fd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath) == 0)
            cache_blocks(*fi, offset, length);
    }
}

void state_monitor::ondelete(const char *filename, const int parentfd)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    char proclnk[32];
    char parentpath[PATH_MAX];

    sprintf(proclnk, "/proc/self/fd/%d", parentfd);
    ssize_t parentlen = readlink(proclnk, parentpath, PATH_MAX);
    if (parentlen > 0)
    {
        // Concat parent dir path and filename to get the full path.
        std::string filepath;
        filepath.reserve(parentlen + strlen(filename) + 1);
        filepath.append(parentpath, parentlen).append("/").append(filename);

        state_file_info *fi;

        // Find out whether we are already tracking this file (eg. due to a previous write/create operation).
        auto fitr = fileinfomap.find(filepath);
        if (fitr != fileinfomap.end())
        {
            fi = &fitr->second;
            if (fi->isnew)
            {
                // If it's a new file, remove from existing entries.
                remove_newfileentry(fi->filepath);
                fileinfomap.erase(fitr);
                return;
            }
        }
        else if (getfileinfo(&fi, filepath) != 0)
        {
            return;
        }

        cache_blocks(*fi, 0, fi->original_length);
    }
}

void state_monitor::ontruncate(const int fd, const off_t newsize)
{
    std::string filepath;
    if (getmappedpath_for_fd(filepath, fd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath) == 0 && newsize < fi->original_length)
            cache_blocks(*fi, 0, fi->original_length);
    }
}

void state_monitor::onclose(const int fd)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    auto pitr = fdpathmap.find(fd);
    if (pitr != fdpathmap.end())
    {
        auto fitr = fileinfomap.find(pitr->second); // pitr->second is the filepath string.
        if (fitr != fileinfomap.end())
            close_cachingfds(fitr->second); // fitr->second is the fileinfo struct.

        fdpathmap.erase(pitr);
    }
}

int state_monitor::getpath_for_fd(std::string &filepath, const int fd)
{
    char proclnk[32];
    sprintf(proclnk, "/proc/self/fd/%d", fd);

    filepath.resize(PATH_MAX);
    ssize_t len = readlink(proclnk, filepath.data(), PATH_MAX);
    if (len > 0)
    {
        filepath.resize(len);
        return 0;
    }
    return -1;
}

int state_monitor::getmappedpath_for_fd(std::string &filepath, const int fd)
{
    // Return path from the map if found.
    const auto itr = fdpathmap.find(fd);
    if (itr != fdpathmap.end())
    {
        filepath = itr->second;
        return 0;
    }

    if (getpath_for_fd(filepath, fd) == 0)
    {
        fdpathmap[fd] = filepath;
        return 0;
    }

    return -1;
}

int state_monitor::getfileinfo(state_file_info **fi, const std::string &filepath)
{
    const auto itr = fileinfomap.find(filepath);
    if (itr != fileinfomap.end())
    {
        *fi = &itr->second;
        return 0;
    }

    state_file_info &fileinfo = fileinfomap[filepath];

    struct stat stat_buf;
    if (stat(filepath.c_str(), &stat_buf) == 0)
        fileinfo.original_length = stat_buf.st_size;
    else
        return -1;

    fileinfo.filepath = filepath;
    *fi = &fileinfo;
    return 0;
}

int state_monitor::cache_blocks(state_file_info &fi, const off_t offset, const size_t length)
{
    // No caching required if this is a new file created during this session.
    if (fi.isnew)
        return 0;

    uint32_t original_blockcount = ceil((double)fi.original_length / (double)BLOCK_SIZE);

    // Check whether we have already cached the entire file.
    if (original_blockcount == fi.cached_blockids.size())
        return 0;

    if (prepare_caching(fi) != 0)
        return -1;

    // Return if incoming write is outside any of the original blocks.
    if (offset > original_blockcount * BLOCK_SIZE)
        return 0;

    uint32_t startblock = offset / BLOCK_SIZE;
    uint32_t endblock = (offset + length) / BLOCK_SIZE;

    std::cout << "Cache blocks: '" << fi.filepath << "' [" << offset << "," << length << "] " << startblock << "," << endblock << "\n";

    // If this is the first time we are caching this file, write an entry to the touched file index.
    if (fi.cached_blockids.empty() && write_touchedfileentry(fi.filepath) != 0)
        return -1;

    for (uint32_t i = startblock; i <= endblock; i++)
    {
        // Check whether we have already cached this block.
        if (fi.cached_blockids.count(i) > 0)
            continue;

        // Read the block being replaced and send to cache file.
        char blockbuf[BLOCK_SIZE];
        if (pread(fi.readfd, blockbuf, BLOCK_SIZE, BLOCK_SIZE * i) <= 0)
        {
            std::cout << "Read failed " << fi.filepath << "\n";
            return -1;
        }

        if (write(fi.cachefd, blockbuf, BLOCK_SIZE) < 0)
        {
            std::cout << "Write to block cache failed\n";
            return -1;
        }

        // Append an entry (44 bytes) into the cache index.
        // Entry format: [blocknum(4 bytes) | cacheoffset(8 bytes) | blockhash(32 bytes)]

        char entrybuf[BLOCKINDEX_ENTRY_SIZE];
        off_t cacheoffset = fi.cached_blockids.size() * BLOCK_SIZE;
        hasher::B2H hash = hasher::hash(blockbuf, BLOCK_SIZE);

        memcpy(entrybuf, &i, 4);
        memcpy(entrybuf + 4, &cacheoffset, 8);
        memcpy(entrybuf + 12, hash.data, 32);
        if (write(fi.indexfd, entrybuf, BLOCKINDEX_ENTRY_SIZE) < 0)
        {
            std::cout << "Write to block index failed\n";
            return -1;
        }

        fi.cached_blockids.emplace(i);
    }

    return 0;
}

int state_monitor::prepare_caching(state_file_info &fi)
{
    if (fi.readfd == 0)
    {
        // Open up the file using a read-only fd.
        fi.readfd = open(fi.filepath.c_str(), O_RDONLY);
        if (fi.readfd < 0)
        {
            std::cout << "Failed to open " << fi.filepath << "\n";
            return -1;
        }

        std::string relpath = fi.filepath.substr(statedir.length(), fi.filepath.length() - statedir.length());

        std::string tmppath;
        tmppath.reserve(scratchdir.length() + relpath.length() + EXT_LEN);

        tmppath.append(scratchdir).append(relpath).append(BLOCKCACHE_EXT);

        // Create directory tree if not exist so we are able to create the cache and index files.
        boost::filesystem::path cachedir = boost::filesystem::path(tmppath).parent_path();
        if (created_dirs.count(cachedir.string()) == 0)
        {
            boost::filesystem::create_directories(cachedir);
            created_dirs.emplace(cachedir.string());
        }

        // Block cache file
        fi.cachefd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT, FILE_PERMS);
        if (fi.cachefd <= 0)
        {
            std::cout << "Failed to open " << tmppath << "\n";
            return -1;
        }

        // Index file
        tmppath.replace(tmppath.length() - EXT_LEN + 1, EXT_LEN - 1, BLOCKINDEX_EXT);
        fi.indexfd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT, FILE_PERMS);
        if (fi.indexfd <= 0)
        {
            std::cout << "Failed to open " << tmppath << "\n";
            return -1;
        }

        // Write first entry (8 bytes) to the index file. First entry is the length of the original file.
        // This can be used when restoring/rolling back a file.
        if (write(fi.indexfd, &fi.original_length, 8) == -1)
        {
            std::cout << "Error writing to index file " << tmppath << "\n";
            return -1;
        }
    }

    return 0;
}

void state_monitor::close_cachingfds(state_file_info &fi)
{
    if (fi.readfd > 0)
        close(fi.readfd);

    if (fi.cachefd > 0)
        close(fi.cachefd);

    if (fi.indexfd > 0)
        close(fi.indexfd);

    fi.readfd = 0;
    fi.cachefd = 0;
    fi.indexfd = 0;
}

int state_monitor::write_touchedfileentry(std::string_view filepath)
{
    if (touchedfileindexfd <= 0)
    {
        std::string indexfile = scratchdir + "/idxtouched.idx";
        touchedfileindexfd = open(indexfile.c_str(), O_WRONLY | O_APPEND | O_CREAT, FILE_PERMS);
        if (touchedfileindexfd <= 0)
        {
            std::cout << "Failed to open " << indexfile << "\n";
            return -1;
        }
    }

    // Write the relative file path to the index.
    filepath = filepath.substr(statedir.length(), filepath.length() - statedir.length());
    write(touchedfileindexfd, filepath.data(), filepath.length());
    write(touchedfileindexfd, "\n", 1);
    return 0;
}

int state_monitor::write_newfileentry(std::string_view filepath)
{
    std::string indexfile = scratchdir + "/idxnew.idx";
    int fd = open(indexfile.c_str(), O_WRONLY | O_APPEND | O_CREAT, FILE_PERMS);
    if (fd <= 0)
    {
        std::cout << "Failed to open " << indexfile << "\n";
        return -1;
    }

    // Write the relative file path to the index.
    filepath = filepath.substr(statedir.length(), filepath.length() - statedir.length());
    write(fd, filepath.data(), filepath.length());
    write(fd, "\n", 1);
    close(fd);
    return 0;
}

void state_monitor::remove_newfileentry(std::string_view filepath)
{
    std::string indexfile = scratchdir + "/idxnew.idx";
    std::string indexfile_tmp = scratchdir + "/idxnew.idx.tmp";

    std::ifstream infile(indexfile);
    std::ofstream outfile(indexfile_tmp);

    bool linestransferred = false;
    for (std::string line; std::getline(infile, line);)
    {
        if (line != filepath) // Skip the file being removed.
        {
            outfile << line << "\n";
            linestransferred = true;
        }
    }

    infile.close();
    outfile.close();

    std::remove(indexfile.c_str());

    if (linestransferred)
        std::rename(indexfile_tmp.c_str(), indexfile.c_str());
    else
        std::remove(indexfile_tmp.c_str());
}

} // namespace fusefs