#ifndef _STATEFS_STATE_RESTORE_
#define _STATEFS_STATE_RESTORE_

#include <string>
#include <unordered_set>
#include <vector>

namespace statefs
{

class state_restore
{
private:
    const std::string statedir, changesetdir;
    std::unordered_set<std::string> created_dirs;
    // Look at new files added and delete them if still exist.
    void delete_newfiles();
    int restore_touchedfiles();
    int read_blockindex(std::vector<char> &buffer, std::string_view file);
    int restore_blocks(std::string_view file, const std::vector<char> &bindex);

public:
    state_restore(const std::string statedir, const std::string changesetdir);
    int restore();
};

} // namespace statefs

#endif
