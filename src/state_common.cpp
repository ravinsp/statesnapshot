#include <string>
#include <boost/filesystem.hpp>
#include "state_common.hpp"

namespace statefs
{

std::string statehistdir;

statedirctx init(const std::string &statehistdir_root)
{
    // Initialize 0 state (current state) directory and return the directory context for it.
    statehistdir = statehistdir_root;
    return get_statedir_context(0, true);
}

std::string get_statedir_root(const int16_t checkpointid)
{
    std::string rootdir = realpath(statehistdir.c_str(), NULL);
    rootdir = rootdir + "/" + std::to_string(checkpointid);
    return rootdir;
}

statedirctx get_statedir_context(const int16_t checkpointid, const bool createdirs)
{
    statedirctx ctx;
    ctx.rootdir = get_statedir_root(checkpointid);
    ctx.datadir = ctx.rootdir + DATA_DIR;
    ctx.blockhashmapdir = ctx.rootdir + BHMAP_DIR;
    ctx.hashtreedir = ctx.rootdir + HTREE_DIR;
    ctx.changesetdir = ctx.rootdir + DELTA_DIR;
    ctx.fusemountdir = ctx.rootdir + FUSE_DIR;

    if (createdirs)
    {
        if (!boost::filesystem::exists(ctx.datadir))
            boost::filesystem::create_directories(ctx.datadir);
        if (!boost::filesystem::exists(ctx.blockhashmapdir))
            boost::filesystem::create_directories(ctx.blockhashmapdir);
        if (!boost::filesystem::exists(ctx.hashtreedir))
            boost::filesystem::create_directories(ctx.hashtreedir);
        if (!boost::filesystem::exists(ctx.changesetdir))
            boost::filesystem::create_directories(ctx.changesetdir);
        if (!boost::filesystem::exists(ctx.fusemountdir))
            boost::filesystem::create_directories(ctx.fusemountdir);
    }

    return ctx;
}

std::string get_relpath(const std::string &fullpath, const std::string &base_path)
{
    std::string relpath = fullpath.substr(base_path.length(), fullpath.length() - base_path.length());
    if (relpath.empty())
        relpath = "/";
    return relpath;
}

std::string switch_basepath(const std::string &fullpath, const std::string &from_base_path, const std::string &to_base_path)
{
    return to_base_path + get_relpath(fullpath, from_base_path);
}

} // namespace statefs