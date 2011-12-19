/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cutils/sockets.h>

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    int fd, ret;
    char* name = "battdsock";
    int type = SOCK_STREAM;
    uid_t uid = 0;
    gid_t gid = 1000;   
    mode_t perm = 0660;

    fd = socket(PF_UNIX, type, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0 , sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), ANDROID_SOCKET_DIR"/%s",
             name);

    ret = unlink(addr.sun_path);
    if (ret != 0 && errno != ENOENT) {
        goto out_close;
    }

    ret = bind(fd, (struct sockaddr *) &addr, sizeof (addr));
    if (ret) {
        goto out_unlink;
    }

    chown(addr.sun_path, uid, gid);
    chmod(addr.sun_path, perm);

    return fd;

out_unlink:
    unlink(addr.sun_path);
out_close:
    close(fd);
    return -1;
}
