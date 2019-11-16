# We are going with NodeJs debian docker image because sample contracts need NodeJs to run.
# Otherwise, hpcore itself can run on any docker image like ubuntu or debian without NodeJs.
FROM node:10.17.0-buster-slim

# Copy fuse shared library and register it.
COPY ./libfuse3.so.3 /usr/local/lib/
RUN ldconfig

# Install fuse.
RUN apt-get update && apt-get install -y fuse && rm -rf /var/lib/apt/lists/*

# hpcore binary is copied to /hp directory withtin the docker image.
WORKDIR /app
COPY ./build/statesnap .
COPY ./build/restore .

# ENTRYPOINT ["/bin/bash"]
ENTRYPOINT ["/app/statesnap", "/statefs/real", "/statefs/fake", "/statefs/cache"]

# docker run --name sfs --rm -t -i --mount type=bind,source=/home/ravin/statefs,target=/statefs statefs

# docker run --name sfs --rm -t -i --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor:unconfined --mount type=bind,source=/home/ravin/statefs,target=/statefs statefs

# docker exec -it sfs /bin/bash
