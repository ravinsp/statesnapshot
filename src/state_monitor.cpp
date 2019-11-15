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
#include "state_monitor.hpp"
#include "hasher.hpp"

namespace fusefs
{

constexpr size_t BLOCK_SIZE = 4 * 1024 ;//* 1024; // 4MB
constexpr size_t INDEX_ENTRY_SIZE = 44;
constexpr size_t EXT_LEN = 8;
const char *const BLOCKCACHE_EXT = ".bcache";
const char *const BLOCKINDEX_EXT = ".bindex";

void state_monitor::oncreate(int fd)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getpath_for_fd(filepath, fd) == 0)
    {
        state_file_info fi;
        fi.isnew = true;
        fileinfomap[filepath] = std::move(fi);
    }
}

void state_monitor::ondelete(const char *filename, int parentfd)
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
            fi = &fitr->second;
        else if (getfileinfo(&fi, filepath) != 0)
            return;

        cache_blocks(*fi, 0, fi->original_length);
    }
}

void state_monitor::onopen(int fd)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getpath_for_fd(filepath, fd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath) == 0)
        {
            // Check whether fd is open in truncate mode.
            int oflags = fcntl(fd, F_GETFL);
            fi->istruncate = (oflags & O_TRUNC);
        }
    }
}

void state_monitor::onwrite(int fd, const off_t offset, const size_t length)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getpath_for_fd(filepath, fd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath) == 0)
            cache_blocks(*fi, offset, length);
    }
}

void state_monitor::onclose(int fd)
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
    // Return path from the map if found.
    const auto itr = fdpathmap.find(fd);
    if (itr != fdpathmap.end())
    {
        filepath = itr->second;
        return 0;
    }

    char proclnk[32];
    char fpath[PATH_MAX];

    sprintf(proclnk, "/proc/self/fd/%d", fd);
    ssize_t len = readlink(proclnk, fpath, PATH_MAX);
    if (len > 0)
    {
        filepath = std::string(fpath, len);
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

    uint32_t startblock, endblock;

    // Check truncate mode.
    if (fi.istruncate)
    {
        startblock = 0;
        endblock = original_blockcount - 1;
    }
    else
    {
        // Return if incoming write is outside any of the original blocks.
        if (offset > original_blockcount * BLOCK_SIZE)
            return 0;

        startblock = offset / BLOCK_SIZE;
        endblock = (offset + length) / BLOCK_SIZE;
    }

    if (open_cachingfds(fi) != 0)
        return -1;

    std::cout << "Cache blocks: '" << fi.filepath << "' " << startblock << "," << endblock << "\n";

    for (uint32_t i = startblock; i <= endblock; i++)
    {
        // Check whether we have already cached this block.
        if (fi.cached_blockids.count(i) > 0)
            continue;

        // Read the block being replaced and send to cache file.
        char blockbuf[BLOCK_SIZE];
        lseek(fi.readfd, BLOCK_SIZE * i, SEEK_SET);
        if (read(fi.readfd, blockbuf, BLOCK_SIZE) < 0)
            return -1;
        if (write(fi.cachefd, blockbuf, BLOCK_SIZE) < 0)
            return -1;

        // Append an entry into the cache index.
        // format: [blockid(4 bytes) | cacheoffset(8 bytes) | blockhash(32 bytes)]

        char entrybuf[INDEX_ENTRY_SIZE];
        off_t cacheoffset = fi.cached_blockids.size() * BLOCK_SIZE;
        hasher::B2H hash = hasher::hash(blockbuf, BLOCK_SIZE);

        memcpy(entrybuf, &i, 4);
        memcpy(entrybuf + 4, &cacheoffset, 8);
        memcpy(entrybuf + 12, hash.data, 32);
        if (write(fi.indexfd, entrybuf, INDEX_ENTRY_SIZE) < 0)
            return -1;

        fi.cached_blockids.emplace(i);
    }

    return 0;
}

int state_monitor::open_cachingfds(state_file_info &fi)
{
    if (fi.readfd == 0)
    {
        // Open up the same file using an independent read-only fd.
        fi.readfd = open(fi.filepath.c_str(), O_RDWR);
        if (fi.readfd < 0)
        {
            std::cout << "Failed to open " << fi.filepath << "\n";
            return -1;
        }

        std::string relpath = fi.filepath.substr(statedir.length(), fi.filepath.length() - statedir.length());

        std::string tmppath;
        tmppath.reserve(scratchdir.length() + relpath.length() + EXT_LEN);

        tmppath.append(scratchdir).append(relpath).append(BLOCKCACHE_EXT);
        // Create directory tree so we are able to create the cache and index files.
        boost::filesystem::create_directories(boost::filesystem::path(tmppath).parent_path());

        // Block cache file
        fi.cachefd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT);
        if (fi.cachefd <= 0)
        {
            std::cout << "Failed to open " << tmppath << "\n";
            return -1;
        }

        // Index file
        tmppath.replace(tmppath.length() - EXT_LEN + 1, EXT_LEN - 1, BLOCKINDEX_EXT);
        fi.indexfd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT);
        if (fi.indexfd <= 0)
        {
            std::cout << "Failed to open " << tmppath << "\n";
            return -1;
        }

        // Write first entry to the index file. First entry is the length of the original file.
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

} // namespace fusefs