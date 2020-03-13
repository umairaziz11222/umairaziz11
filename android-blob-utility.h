/*
 * Android blob utility
 *
 * Copyright (C) 2014 JackpotClavin <jonclavin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef _ANDROID_BLOB_UTILITY_H_
#define _ANDROID_BLOB_UTILITY_H_

#define _GNU_SOURCE
#include <stdlib.h>

#define MAX_LIB_NAME 50
#define ALL_LIBS_SIZE 16384 /* 16KB */

/* #define DEBUG */

/* Change value below to match your /system dump's SDK version. */
/* See: https://developer.android.com/guide/topics/manifest/uses-sdk-element.html#ApiLevels */
#define SYSTEM_DUMP_SDK_VERSION 19 /* Android KitKat*/

#define SYSTEM_DUMP_ROOT "/home/android/system_dump"

#define SYSTEM_VENDOR "manufacturer"
#define SYSTEM_DEVICE "device"

const char *blob_directories[] = {
    "/vendor/lib64/egl/",
    "/vendor/lib/egl/",
    "/vendor/lib64/hw/",
    "/vendor/lib/hw/",
    "/vendor/lib64/",
    "/vendor/lib/",
    "/vendor/bin/",
    "/lib64/egl/",
    "/lib/egl/",
    "/lib64/hw/",
    "/lib/hw/",
    "/lib64/",
    "/lib/",
    "/usr/lib/",
    "/usr/lib/alsa-lib/",
    "/bin/",
    NULL
};

const char *lib_beginning = "lib";
const char *egl_beginning = "egl";

const char *lib_ending = ".so";

#endif /* _ANDROID_BLOB_UTILITY_H_ */
