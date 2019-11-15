#ifndef _FS_HANDLER_
#define _FS_HANDLER_

#include <cstdint>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace fusefs
{

struct state_file_info
{
    bool isnew;
    bool istruncate;
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
    std::mutex fmapmutex;

    int touchedfileindexfd = 0;

    int getpath_for_fd(std::string &filepath, const int fd);
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
    void oncreate(int fd);
    void onopen(int fd);
    void ondelete(const char *filename, int parentfd);
    void onwrite(int fd, const off_t offset, const size_t length);
    void onclose(int fd);
};

} // namespace fusefs

#endif