#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unordered_map>
#include <cmath>
#include "state_monitor.hpp"

namespace fusefs
{

const size_t BLOCK_SIZE = 4; // 4 * 1024 * 1024; // 4MB
const int EXT_LEN = 8;
const char *const BLOCKCACHE_EXT = ".bcache";
const char *const BLOCKINDEX_EXT = ".bindex";

void state_monitor::onwrite(int fd, const off_t offset, const size_t length)
{
    std::lock_guard<std::mutex> lock(fmapmutex);

    std::string filepath;
    if (getpath_for_fd(filepath, fd) == 0)
    {
        state_file_info *fi;
        if (getfileinfo(&fi, filepath, fd) == 0)
        {
            cache_blocks(*fi, fd, offset, length);
        }
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

int state_monitor::getfileinfo(state_file_info **fi, const std::string &filepath, const int fd)
{
    const auto itr = fileinfomap.find(filepath);
    if (itr != fileinfomap.end())
    {
        *fi = &itr->second;
        return 0;
    }

    struct stat stat_buf;
    if (fstat(fd, &stat_buf) == 0)
    {
        state_file_info &fileinfo = fileinfomap[filepath];
        fileinfo.original_blockcount = ceil((double)stat_buf.st_size / (double)BLOCK_SIZE);
        fileinfo.filepath = filepath;
        fileinfo.readfd = 0;
        fileinfo.cachefd = 0;
        fileinfo.indexfd = 0;

        *fi = &fileinfo;
        return 0;
    }

    return -1;
}

int state_monitor::cache_blocks(state_file_info &fi, const int fd, const off_t offset, const size_t length)
{
    uint64_t original_blocklen = fi.original_blockcount * BLOCK_SIZE;

    // Check if offset is outside the original block length.
    if (offset > original_blocklen || offset + length > original_blocklen)
        fi.newblocks_added = true;

    // Return of incoming write is outside of the original block length.
    if (offset > original_blocklen)
        return 0;

    // Check whether we have already cached the entire file.
    if (fi.original_blockcount == fi.cached_blockids.size())
        return 0;

    uint32_t startblock, endblock;

    // Calculate block ids to cache in this operation.
    // Check whether fd is open in truncate mode. If so we need to cache the entire file
    // before it gets overwritten.
    int oflags = fcntl(fd, F_GETFL);
    if (oflags & O_TRUNC)
    {
        startblock = 0;
        endblock = fi.original_blockcount - 1;
    }
    else
    {
        startblock = offset / BLOCK_SIZE;
        endblock = (offset + length) / BLOCK_SIZE;
    }

    for (int i = startblock; i <= endblock; i++)
    {
        // Check whether we have already cached the block.
        if (fi.cached_blockids.count(i) > 0)
            continue;

        if (open_cachingfds(fi, fd) != 0)
            return -1;

        // Read the block being replaced and send to cache file.
        char buf[BLOCK_SIZE];
        lseek(fi.readfd, BLOCK_SIZE * i, SEEK_SET);
        read(fi.readfd, buf, BLOCK_SIZE);
        write(fi.cachefd, buf, BLOCK_SIZE);

        fi.cached_blockids.emplace(i);
    }

    return 0;
}

int state_monitor::open_cachingfds(state_file_info &fi, const int originalfd)
{
    if (fi.readfd == 0)
    {
        // Open up the same file using an independent read-only fd.
        char proclnk[32];
        sprintf(proclnk, "/proc/self/fd/%d", originalfd);
        fi.readfd = open(proclnk, O_RDWR); // Opening fd via /proc will create independent fd.
        if (fi.readfd < 0)
            return -1;
    }

    if (fi.cachefd == 0)
    {
        // Get original filepath relative to the state directory.
        std::string_view orifile = fi.filepath.substr(monitoreddir.length(), (fi.filepath.length() - monitoreddir.length()));

        std::string tmppath;
        tmppath.reserve(scratchdir.length() + orifile.length() + EXT_LEN);

        tmppath.append(scratchdir).append(orifile).append(BLOCKCACHE_EXT);
        fi.cachefd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT);
        if (fi.cachefd <= 0)
            return -1;

        tmppath.replace(tmppath.length() - EXT_LEN, EXT_LEN, BLOCKINDEX_EXT);
        fi.indexfd = open(tmppath.c_str(), O_WRONLY | O_APPEND | O_CREAT);
        if (fi.indexfd <= 0)
            return -1;
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