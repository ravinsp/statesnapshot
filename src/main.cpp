#include <iostream>
#include <string>
#include <thread>
#include <csignal>
#include "fusefs.hpp"

std::thread fusethread;

int main(int argc, char *argv[])
{
    // We need an fd for every dentry in our the filesystem that the
    // kernel knows about. This is way more than most processes need,
    // so try to get rid of any resource softlimit.
    fusefs::maximize_fd_limit();

    //fusethread = std::thread([&] {
        fusefs::start(argv[0], argv[1], argv[2], argv[3]);
    //});

    //fusethread.join();
}