#include <iostream>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include "fusefs.hpp"

std::thread fusethread;

int main(int argc, char *argv[])
{
    if (argc !=4)
        exit(1);

    // We need an fd for every dentry in our the filesystem that the
    // kernel knows about. This is way more than most processes need,
    // so try to get rid of any resource softlimit.
    fusefs::maximize_fd_limit();

    const char *sourcedir = argv[1];
    const char *mountpoint = argv[2];
    const char *scratchdir = argv[3];

    boost::filesystem::create_directories(mountpoint);
    boost::filesystem::create_directories(scratchdir);

    //fusethread = std::thread([&] {
    fusefs::start(argv[0],
                  realpath(sourcedir, NULL),
                  realpath(mountpoint, NULL),
                  realpath(scratchdir, NULL));
    //});

    //fusethread.join();
}