#include <string>

namespace statefs
{

const std::string get_relpath(const std::string &fullpath, const std::string &base_path)
{
    std::string relpath = fullpath.substr(base_path.length(), fullpath.length() - base_path.length());
    if (relpath.empty())
        relpath = "/";
    return relpath;
}

const std::string switch_basepath(const std::string &fullpath, const std::string &from_base_path, const std::string &to_base_path)
{
    return to_base_path + get_relpath(fullpath, from_base_path);
}

} // namespace statefs