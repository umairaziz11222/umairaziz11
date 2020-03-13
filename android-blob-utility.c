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

#include "android-blob-utility.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

bool dot_so_finder(char *filename);
void check_emulator_for_lib(char *emulator_check);

char system_dump_root[256] = SYSTEM_DUMP_ROOT;

char system_vendor[32] = SYSTEM_VENDOR;

char system_device[32] = SYSTEM_DEVICE;

char all_libs[ALL_LIBS_SIZE] = {0};
char *sdk_buffer;

int sdk_version = SYSTEM_DUMP_SDK_VERSION;

/* The purpose of this program is to help find proprietary libraries that are needed to
 * build AOSP-based ROMs. Running the top command on the stock ROM will help find proprietary
 * daemons that are started by the init*.rc scripts, and are normally-located in /system/bin/
 * For instance, Running this program on "/system/bin/mm-qcamera-daemon" will not-only find
 * the libraries needed that the linker tell you that you need, but also the libraries that
 * aren't listed as shared libraries at build time, which could slip past the linker but still
 * (probably) not allow the daemon to run correctly, or at all, and the worst part is because the
 * linker is satisfied, it won't display any error message about missing libraries. Since this
 * program mmaps the entire library and finds every instance of ".so" in libraries, it also will
 * recursively run the ".so" finder on *THOSE* libraries needed by the original file, so it
 * theoretically should spit back every single library needed to run the daemon, or a file such
 * as /system/lib/hw/camera.<board>.so will also work. The way to use this program is to dump the
 * /system of your stock ROM, or extract a custom-recovery backup. When the program prompts you
 * for the system dump root, you type "/home/user/backup/dump/system" (without quotes) so that
 * typing the command "ls /home/user/backup/dump/system/build.prop" it would yield the dump's
 * build.prop. It will then analyze which proprietary files are missing from the emulator, thus,
 * you will have to put those files into the AOSP-based ROM to get the daemon/library to run.
 * Note: just because the library is present in the emulator doesn't mean you don't need the dump's
 * proprietary version to get the daemon to run! A proprietary /system/bin/rild might need its own
 * /system/lib/libril.so, even though this program will fail to mention libril.so, because it's in
 * the emulator!
 */

/* Check to see if the characters normally appear in the name of libraries, and not an instruction
 * which might look like &^%@.so
 */

bool char_is_valid(char *s) {

    if (*s >= 'a' && *s <= 'z')
        return true;
    if (*s >= 'A' && *s <= 'Z')
        return true;
    if (*s >= '0' && *s <= '9')
        return true;
    if (*s == '_' || *s == '-')
        return true;
    if (*s == 0)
        return true;
    if (*s == '%') /* wildcard, bitches! */
        return true;
    return false;
}

/* No need to print out libdiag.so 100 times, so if it's the first time, add it to the list
 * of libraries that we have found that are missing and be done with it.
 */

bool check_if_repeat(char *lib) {

    if (memmem(all_libs, ALL_LIBS_SIZE, lib, strlen(lib))) {
        /* fprintf(stderr, "skipping %s!!\n", lib); */
        return true;
    }
    return false;
}

/* If it's the first time a library is found, add it do the repository of libraries that
 * have been mentioned. There is no need to keep spitting out the same library 100 times
 * if it's needed by multiple libraries.
 */

void mark_lib_as_processed(char *lib) {

    static int offset = 0;
#ifdef DEBUG
    const char *save = lib;
#endif

    while (*lib) {
        all_libs[offset] = *lib;
        offset++;
        lib++;
    }
    offset++;
    if (offset > ALL_LIBS_SIZE - 100)
        fprintf(stderr, "You may need to increase the ALL_LIBS_SIZE macro.\n");
#ifdef DEBUG
    fprintf(stderr, "Added: %s %d\n", save, offset);
#endif
}

/* Just a little function to check if the user has inputted the correct folder that this program
 * is expecting to receive from the user
 */

bool build_prop_checker(void) {

    char buildprop_checker[256];
    char *line, *value;
    long l;
    size_t n;
    FILE *fp;

    sprintf(buildprop_checker, "%s/build.prop", system_dump_root);
    fp = fopen(buildprop_checker, "r");
    if (! fp) {
        fprintf(stderr, "Error: build.prop file not found in system dump's root.\n");
        fprintf(stderr, "Your path to the system dump is not correct.\n");
        fprintf(stderr, "The command:\n");
        fprintf(stderr, "\"ls %s/build.prop\"\n", system_dump_root);
        fprintf(stderr, "should yield the system dump's build.prop file.\n");
        fprintf(stderr, "Exiting!\n");
        return true;
    }
    while (!feof(fp)) {
        n = 0;
        line = NULL;
        getline(&line, &n, fp);
        value = strchr(line, '=');
        if (value) {
            *value++ = '\0';
            l = strlen(value) - 1;
            while (l >= 0 && value[l] == '\n')
            {
                value[l] = '\0';
                l--;
            }
            if (!strcmp(line, "ro.build.version.sdk"))
                sdk_version = atoi(value);
            if (!strcmp(line, "ro.product.brand"))
                strcpy(system_vendor, value);
            if (!strcmp(line, "ro.product.device"))
                strcpy(system_device, value);
        }
        free(line);
    }
    fclose(fp);
    return false;
}

/* See if the filename in the /system dump matches a file in the SDK version's emulator dump.
 * if it is not in the emulator's dump, it means it's a proprietary or must be built from source
 * in order for the library of daemon to run.
 */

bool check_emulator_files_for_match(char *emulator_full_path) {
    char *p;

    p = strstr(sdk_buffer, emulator_full_path);
    if (p && *(p - 1) != '#')
        return true;
    return false;
}

/* Receive two strings; the first part of the library, and the second part. Then look in the library
 * directories for libraries which begin and end with its received parameters; then pass them to the
 * check_emulator_for_lib function
 */

bool find_wildcard_libraries(char *beginning, char *end) {

    DIR *dir;
    struct dirent *dirent;
    char full_path[256] = {0};
    int i;
    bool found = false;

    if (strchr(end, '%') && strstr(end, lib_ending))
        end = strstr(end, lib_ending);

    for (i = 0; blob_directories[i]; i++) {
        sprintf(full_path, "%s%s", system_dump_root, blob_directories[i]);
        dir = opendir(full_path);
        if (!dir)
            continue;

        while ((dirent = readdir(dir)) != NULL) {
            if (strstr(dirent->d_name, beginning) && strstr(dirent->d_name, end)) {
                check_emulator_for_lib(dirent->d_name);
                found = true;
            }
        }
        closedir(dir);
    }

    if (!found)
        fprintf(stderr, "warning: wildcard %s%%s%s missing or broken\n", beginning, end);
    return found;
}

/* This function will split the wildcard library name into two parts; the beginning part,
 * and the end part. The wildcard string 'libmmcamera_%s.so' will be split into "libmmcamera_"
 * and ".so", then passed to find_wildcard_libraries, where that function will search for libraries
 * beginning with "libmmcamera_", and ending with ".so" and pass its hits over check_emulator_for_lib.
 */

bool process_wildcard(char *wildcard) {

    char *ptr;
    char beginning[64] = {0};
    char end[64] = {0};

    ptr = strchr(wildcard, '%');
    if (ptr) {
        strncpy(beginning, wildcard, ptr - wildcard);
        ptr += 2; /* advance beyond the format specifier (normally %s or possibly %c) */
        strcpy(end, ptr);
    }

    return find_wildcard_libraries(beginning, end);
}

/* This checks to see if the library that is called/mentioned or in another library or daemon is even
 * in the /system dump. There may be a few obsolete references to old libraries that are no longer used.
 * If it is looking for 'libfoo.so' and it indeed finds 'libfoo.so', we print it formatted for use in the
 * vendor directory with "vendor/../../../libfoo.so". If it doesn't find a hit, it gets printed that it's
 * not even in the /system folder (obsolete or something), this will also give us a notification if the
 * program messed up, or if there is a new naming scheme for libraries that this program is not accustomed
 * to, instead of silently failing without ever mentioning it
 */

bool get_lib_from_system_dump(char *system_check) {

    int i;
    char system_dump_path_to_blob[256];
    bool found_hit = false;

    for (i = 0; blob_directories[i]; i++) {
        sprintf(system_dump_path_to_blob, "%s%s%s", system_dump_root, blob_directories[i],
                system_check);
        if (!access(system_dump_path_to_blob, F_OK)) {
            printf("vendor/%s/%s/proprietary%s%s:system%s%s \\\n", system_vendor, system_device,
                    blob_directories[i], system_check, blob_directories[i], system_check);
            found_hit = dot_so_finder(system_dump_path_to_blob);
        }
    }

    /* if we've made it this far, it means that the blob was in neither the emulator nor the
     * actual system dump, meaning it is an obsolete reference to a no-longer used blob that
     * was never removed, or more likely, a wildcard in the form of libmmcamera_%s.so, so
     * process the wildcard accordingly, or print out that it's an obsolete reference, or
     * possibly a program fuck-up.
     */
    if (strchr(system_check, '%')) {
        return process_wildcard(system_check);
    }

    if (!found_hit)
        fprintf(stderr, "warning: blob file %s missing or broken\n", system_check);
    return found_hit;
}

/* We scan through the emulator's library directories and see if there's a hit. If there is,
 * we don't display anything. If there is no hit, we hand it over to the function called
 * get_lib_from_system_dump.
 */

void check_emulator_for_lib(char *emulator_check) {

    char emulator_full_path[256];
    int i;

    if (check_if_repeat(emulator_check))
        return;

    for (i = 0; blob_directories[i]; i++) {
        sprintf(emulator_full_path, "/system%s%s", blob_directories[i], emulator_check);
        /* don't do anything if the file is in the emulator, as that means it's not proprietary. */
        if (check_emulator_files_for_match(emulator_full_path))
            return;
    }

    mark_lib_as_processed(emulator_check); /* mark the library as processed */

    /* if we've made it this far, the blob is NOT in the emulator so that means it is proprietary
     * or an obsolete reference to a blob that is not even in the system dump.
     */
    get_lib_from_system_dump(emulator_check);
}

/* After receiving a pointer to a location of memory that contains the string ".so" and
 * does not have a random bogus character before that which was filtered by the said
 * char_is_valid(prepeek), we now work our way backwards in memory to find find the string
 * "lib" or in rare cases "egl" (eglsubAndroid.so) and break out of the loop once we find
 * a match. We save the pointer to the period ".so", and add 3. Then we subtract that location
 * in memory from the instance of "lib" or "egl" so that value is the entire length of the lib
 * | lib_whatever.so | then strncpy the value into "full_name", and pass it to the check_emulator_for_lib
 * method which will search through the libraries directories of the emulator to see if there's
 * a library with that name that matches the one sent by get_full_lib_name. If it's missing, it means
 * that the library referenced is *not* in the emulator, which means:
 *
 * A. The file is a proprietary file, meaning it's needed by the service, and should be copied
 * into your vendor folder of your Android source tree, and referenced by your device source
 * tree with : $(call inherit-product-if-exists, vendor/manufacturer/codename/codename-vendor.mk)
 * (A quick word of notice is that just because the file is not in the emulator's system dump,
 * does not necessarily mean that the file is proprietary, it could just be that the emulator
 * does not need this file to be built, and is not built, so it may throw off this program. A
 * quick work around would be to type "mgrep lib_whatever" in your Android source tree's root,
 * and seeing if there are any hits in an Android.mk file. If there are, see B.
 *
 * B. It must be explicitly built and thus required in your device folder to be built such as
 * PRODUCT_PACKAGES += lib_whatever
 *
 * C. The library mentioned by the original file does not even exist in your device folder (it
 * should printf a message saying it's in neither the emulator not your system dump, this happens
 * occasionally)
 *
 * D. The algorithm fucked up (sorry) in the worst possible case, something will segfault, and you
 * will see this by it either saying segfault, or the message at the bottom of the main method
 * "Completed successfully." will fail to appear.
 */

void get_full_lib_name(char *found_lib) {

    char *ptr, *peek;

    char full_name[256] = {0};

    long len;
    int num_chars;
    int i;

    ptr = found_lib;
    peek = ptr - 1;

    /* if there's a false-positive in finding matching ".so", but it isn't ever referencing
     * a library, it's probably just instructions that slipped through the cracks. In this case
     * we will rewind the pointer that's searching for "lib" or "egl" MAX_LIB_NAME (default 50)
     * times, in which we will bail out citing that it was probably a false-positive
     */
    for (num_chars = 0; num_chars <= MAX_LIB_NAME; num_chars++) {
        if (!strncmp(ptr, egl_beginning, strlen(egl_beginning)) || !strncmp(ptr, lib_beginning, strlen(lib_beginning))) {
            peek = ptr - 1;
            /* the peek below would fall victim to a file which is looking directly for
             * "/system/lib/lib_whatever.so", because it would now point to lib/lib_whatever.so
             * which is not what what we want, so take the first pick if the peek character is '/'
             */
            if (*peek == '/') {
                for (i = 0; blob_directories[i]; i++) {
                    if (!strncmp(peek, blob_directories[i], strlen(blob_directories[i]))) {
                        peek += strlen(blob_directories[i]);
                        ptr = peek;
                        break;
                    }
                }
                break;
            }
            /* some libraries are called "libmmcamera_wavelet_lib.so", in which the pointer will
             * rewind to the first "lib" and then will pass it over to the check_emulator_for_lib
             * method, which will in turn bark about a missing "lib.so", so we will rewind the pointer
             * some extra times until it encounters an invalid character using the char_is_valid
             * function and if it ends up finding another instance of "lib", picks that *that* one, not
             * the original one, so we will get the entire library name of "libmmcamera_wavelet_lib.so"
             * and not just "lib.so" which would have been chosen if not for the peek.
             */
            while (char_is_valid(peek) && *peek--) {
                if (!strncmp(peek, lib_beginning, strlen(lib_beginning))) {
#ifdef DEBUG
                    fprintf(stderr, "Possible lib_lib.so! %s\n", peek);
#endif
                    ptr = peek;
                }
            }
            break;
        }
        if (num_chars == MAX_LIB_NAME) {
#ifdef DEBUG
            fprintf(stderr, "Character limit exceeded! Full string was:\n");
            for (num_chars = 0; num_chars < MAX_LIB_NAME + strlen(lib_beginning); num_chars++) {
                fprintf(stderr, "%c", *ptr);
                ptr++;
            }
            fprintf(stderr, "\n");
#endif
            return;
        }
        ptr--;
        peek--;
    }
    len = (long)(found_lib + strlen(lib_beginning)) - (long)ptr;
    strncpy(full_name, ptr, len);

    check_emulator_for_lib(full_name);
}

/* Purpose of this method is to open the library, by mmap-ing it, and traversing until it
 * until it finds ".so", (the ending of most Linux library names.) and then it will hand
 * it to the get_full_lib_name method. The "prepeek" pointer checks to make sure that the
 * character before the period in ".so" is a valid character (defined at the bottom of the
 * source) to cut down on false-positives where random binary-file junk just-so-happens to
 * have a random "][#$@#FW@&&.+^.so" laying around that doesn't pertain to a library, and
 * is just normal binary-file instructions and whatnot.
 */

bool dot_so_finder(char *filename) {

    int file_fd;

    char *file_map;
    char *ptr;
    char *prev;
    off_t size;
    struct stat file_stat;

    file_fd = open(filename, O_RDONLY);
    if (file_fd == -1) {
        fprintf(stderr, "File %s not found!\n", filename);
        return false;
    }

    fstat(file_fd, &file_stat);

    file_map = mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);

    ptr = file_map;
    prev = ptr;
    size = file_stat.st_size;

    while ((ptr = memmem(ptr, size, lib_ending, strlen(lib_ending))) != NULL) {

        if (ptr >= file_map + file_stat.st_size)
            break;

        if (char_is_valid(ptr - 1))
            get_full_lib_name(ptr);

        size -= ptr - prev;
        prev = ptr;

        ptr++; /* Advance pointer one character to ensure we don't keep looping
                  over the same ".so" instance over and over again */
    }

    munmap(file_map, file_stat.st_size);
    close(file_fd);
    return true;
}

void remove_unwanted_characters(char *input) {

    char *p;

    /* If the received string is actually '/home/android/dump', with
     * apostrophes, shift elements back one index to remove the front '
     */

    if (*input == '\'')
        memmove(&input[0], &input[1], strlen(input));

    p = strrchr(input, ' '); /* turn possible space at end to null */
    if (p)
        *p = '\0';

    p = strrchr(input, '\''); /* turn possible apostrophe at end to null */
    if (p)
        *p = '\0';

    p = strrchr(input, '\n'); /* turn possible newline at end to null */
    if (p)
        *p = '\0';

    p = input + strlen(input); /* remove final slash in /home/android/dump/ */
    if (*(p - 1) == '/')
        *(p - 1) = '\0';
}

void read_user_input(char *input, int len, char *fmt) {

    char message[256];
    char res[256];
#ifdef USE_READLINE
    char *tmp;
#endif

    sprintf(message, fmt, input);
#ifndef USE_READLINE
    fprintf(stderr, "%s", message);
    fgets(res, sizeof res, stdin);
#else
    rl_outstream = stderr;
    tmp = readline(message);
    if (tmp)
    {
        strncpy(res, tmp, sizeof res);
        free(tmp);
    }
    else
        res[0] = '\0';
#endif

    remove_unwanted_characters(res);
    if (res[0])
        strncpy(input, res, len);
}

int main(int argc, char **argv) {

    char *last_slash;
    char emulator_system_file[32], *sdkversionstr;
    size_t n;
    int num_files;
    long length = 0;
    FILE *fp;

    char filename_buf[256];
    char *filename = filename_buf;

#ifndef VARIABLES_PROVIDED
    read_user_input(system_dump_root, sizeof(system_dump_root), "System dump root?\n");

    if (build_prop_checker())
        return 1;

    read_user_input(system_vendor, sizeof(system_vendor), "Target vendor name [%s]?\n");
    read_user_input(system_device, sizeof(system_device), "Target device name [%s]?\n");

    fprintf(stderr, "System dump SDK version? [%d]\n", sdk_version);
    fprintf(stderr, "See: https://developer.android.com/guide/topics/manifest/uses-sdk-element.html#ApiLevels\n");
    sdkversionstr = NULL;
    n = 0;
    getline(&sdkversionstr, &n, stdin);
    if (isdigit(*sdkversionstr))
        sdk_version = atoi(sdkversionstr);
    free(sdkversionstr);
#endif

    sprintf(emulator_system_file, "emulator_systems/sdk_%d.txt", sdk_version);
    fp = fopen(emulator_system_file, "r");
    if (!fp) {
        fprintf(stderr, "SDK text file %s not found, exiting!\n", emulator_system_file);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    rewind(fp);

    sdk_buffer = (char*)malloc(sizeof(char) * length);
    fread(sdk_buffer, 1, length, fp);
    fclose(fp);


    fprintf(stderr, "How many files?\n");
    scanf("%d%*c", &num_files);

    while (num_files) {
        fprintf(stderr, "Files to go: %d\n", num_files);

        read_user_input(filename, sizeof(filename_buf), "File name?\n");

        if (get_lib_from_system_dump(filename))
        {
            last_slash = strrchr(filename, '/');
            if (last_slash)
                check_emulator_for_lib(++last_slash);
            num_files--;
        }
    }

    fprintf(stderr, "Completed successfully.\n");
    free(sdk_buffer);
    argc = argc;
    argv = argv;

    return 0;
}
