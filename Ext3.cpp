/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>

#include <linux/ext2_fs.h>
#include <linux/ext3_fs.h>

#include <linux/kdev_t.h>

#define LOG_TAG "Vold"

#include <cutils/log.h>
#include <cutils/properties.h>

#include "Ext3.h"

static char E2FSCK_PATH[] = "/system/bin/e2fsck";
static char MKE2FS_PATH[] = "/system/bin/mke2fs";

extern "C" int logwrap(int argc, const char **argv, int background);
extern "C" int mount(const char *, const char *, const char *, unsigned long, const void *);

int Ext3::identify(const char *fsPath)
{
    int rc = -1;
    int fd;
    struct ext3_super_block sb;

    if ((fd = open(fsPath, O_RDWR)) < 0) {
        SLOGE("Unable to open device '%s' (%s)", fsPath,
             strerror(errno));
        return -errno;
    }

    if (lseek(fd, 1024, SEEK_SET) < 0) {
        SLOGE("Unable to lseek to get superblock (%s)", strerror(errno));
        rc =  -errno;
        goto out;
    }

    if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        SLOGE("Unable to read superblock (%s)", strerror(errno));
        rc =  -errno;
        goto out;
    }

    if (sb.s_magic == EXT2_SUPER_MAGIC ||
        sb.s_magic == EXT3_SUPER_MAGIC)
        rc = 0;
    else
        rc = -ENODATA;

 out:
    //SLOGI("ext_identify(%s): rc = %d", fsPath, rc);
    close(fd);
    return rc;
}

int Ext3::check(const char *fsPath) {
    bool rw = true;
    if (access(E2FSCK_PATH, X_OK)) {
        SLOGW("Skipping fs checks\n");
        return 0;
    }

    int rc = 0;
    do {
        const char *args[5];
        args[0] = E2FSCK_PATH;
        args[1] = "-v";
        args[2] = "-p";
        args[3] = fsPath;
        args[4] = NULL;

        rc = logwrap(4, args, 1);

        switch(rc) {
        case 0:
            SLOGI("Filesystem had no errors");
            return 0;

        case 1:
        	SLOGI("Filesystem had corrected errors");
        	return 0;

        case 2:
            SLOGE("Filesystem had corrected errors (system should be rebooted)");
            errno = EIO;
            return -1;

        case 4:
            SLOGE("Filesystem had uncorrectable errors");
            errno = EIO;
            return -1;

        case 8:
            SLOGE("Operational error while checking filesystem");
            errno = EIO;
            return -1;

        default:
            SLOGE("Filesystem check failed (unknown exit code %d)", rc);
            errno = EIO;
            return -1;
        }
    } while (0);

    return 0;
}

int Ext3::doMount(const char *fsPath, const char *mountPoint,
                 bool ro, bool remount) {

    char *fs[] = { "ext3", "ext2", NULL };

    int flags, rc = 0;

    flags = MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_NOATIME | MS_NODIRATIME;
    flags |= (ro ? MS_RDONLY : 0);
    flags |= (remount ? MS_REMOUNT : 0);

    char **f;
    for (f = fs; *f != NULL; f++) {
        rc = mount(fsPath, mountPoint, *f, flags, "user_xattr");
        //SLOGE("EXT mount returned: %d", rc);
        if (rc && errno == EROFS) {
            SLOGE("ext_mount(%s, %s): Read only filesystem - retrying mount RO",
                 fsPath, mountPoint);
            flags |= MS_RDONLY;
            rc = mount(fsPath, mountPoint, *f, flags, "user_xattr");
        }
        if (!rc)
            break;
    }

    // Chmod the mount point so that its a free-for-all.
    // (required for consistency with VFAT.. sigh)
    if (chmod(mountPoint, 0777) < 0) {
        SLOGE("Failed to chmod %s (%s)", mountPoint, strerror(errno));
        return -errno;
    }

    return rc;
}

int Ext3::format(const char *fsPath, unsigned int numSectors) {
    const char *args[7];
    int rc;

    args[0] = MKE2FS_PATH;
    args[1] = "-b 4096";
    args[2] = "-m 1";
    args[3] = "-L android";
    args[4] = "-v";
    args[5] = fsPath;
    args[6] = NULL;
    rc = logwrap(6, args, 1);

    if (rc == 0) {
        SLOGI("Filesystem formatted OK");
        return 0;
    } else {
        SLOGE("Format failed (unknown exit code %d)", rc);
        errno = EIO;
        return -1;
    }
    return 0;
}
