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
    off_t original_length;
    std::unordered_set<uint32_t> cached_blockids;
    std::string_view filepath;
    bool newblocks_added;
    int readfd;
    int cachefd;
    int indexfd;
};

class state_monitor
{
private:
    std::unordered_map<int, std::string> fdpathmap;
    std::unordered_map<std::string, state_file_info> fileinfomap;
    std::mutex fmapmutex;

    int getpath_for_fd(std::string &filepath, const int fd);
    int getfileinfo(state_file_info **fileinfo, const std::string &filepath, const int fd);
    int cache_blocks(state_file_info &fi, const int fd, const off_t offset, const size_t length);

    int open_cachingfds(state_file_info &fi, const int originalfd);
    void close_cachingfds(state_file_info &fi);


public:
    std::string monitoreddir;
    std::string scratchdir;
    void onwrite(int fd, const off_t offset, const size_t length);
    void onclose(int fd);
};

} // namespace fusefs

#endif