# statesnapshot

Sate management prototype for [Hot Pocket](https://github.com/HotPocketDev/core)

### Install libfuse

1. `sudo apt-get install -y meson ninja-build pkg-config`
2. Download [libfuse 3.8](https://github.com/libfuse/libfuse/releases/download/fuse-3.8.0/fuse-3.8.0.tar.xz) and extract.
3. `mkdir build; cd build`
4. `meson .. && ninja`
6. `sudo ninja install`