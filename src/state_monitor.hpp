#ifndef _FS_HANDLER_
#define _FS_HANDLER_

#include <cstdint>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <boost/filesystem.hpp>

namespace fusefs
{

struct state_file_info
{
    bool isnew;
    off_t original_length;
    std::unordered_set<uint32_t> cached_blockids;
    std::string filepath;
    int readfd;
    int cachefd;
    int indexfd;
};

class state_monitor
{
private:
    std::unordered_map<int, std::string> fdpathmap;
    std::unordered_map<std::string, state_file_info> fileinfomap;
    std::unordered_set<std::string> touchedfiles;
    std::unordered_set<std::string> created_dirs;
    std::mutex fmapmutex;

    int touchedfileindexfd = 0;

    int getpath_for_fd(std::string &filepath, const int fd);
    int getmappedpath_for_fd(std::string &filepath, const int fd);
    int getfileinfo(state_file_info **fileinfo, const std::string &filepath);
    int cache_blocks(state_file_info &fi, const off_t offset, const size_t length);

    int prepare_caching(state_file_info &fi);
    void close_cachingfds(state_file_info &fi);
    int write_touchedfileentry(std::string_view filepath);
    int write_newfileentry(std::string_view filepath);
    void remove_newfileentry(std::string_view filepath);

public:
    std::string statedir;
    std::string scratchdir;
    void oncreate(const int fd);
    void onopen(const int inodefd, const int flags);
    void onwrite(const int fd, const off_t offset, const size_t length);
    void ondelete(const char *filename, const int parentfd);
    void ontruncate(const int fd, const off_t newsize);
    void onclose(const int fd);
};

} // namespace fusefs

#endif