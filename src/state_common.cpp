#include <string>
#include <boost/filesystem.hpp>
#include "state_common.hpp"

namespace statefs
{

statedirctx get_statedir_context(const std::string statehistdir, const int16_t stateid, const bool createdirs)
{
    std::string rootdir = realpath(statehistdir.c_str(), NULL);
    rootdir.append("/0");

    statedirctx ctx;
    ctx.rootdir = rootdir;
    ctx.datadir = rootdir + "/data";
    ctx.blockhashmapdir = rootdir + "/bhmaps";
    ctx.hashtreedir = rootdir + "/htree";
    ctx.changesetdir = rootdir + "/delta";

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
    }

    return ctx;
}

void create_checkpoint(const std::string statehistdir)
{
    int16_t n = 0;
    bool prevstatedir_existed;

    do
    {
        statedirctx ctx_n = get_statedir_context(statehistdir, n, n == 0);
        statedirctx ctx_n1 = get_statedir_context(statehistdir, n - 1, false);

        prevstatedir_existed = boost::filesystem::exists(ctx_n1.rootdir);

        if (!prevstatedir_existed)
            boost::filesystem::create_directories(ctx_n1.changesetdir);

        boost::filesystem::rename(ctx_n1.changesetdir, (ctx_n1.changesetdir + ".tmp"));

        boost::filesystem::rename(ctx_n.changesetdir, ctx_n1.changesetdir);

        n--;

    } while (prevstatedir_existed);
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