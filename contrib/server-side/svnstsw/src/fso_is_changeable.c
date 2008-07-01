/*
 * Copyright (c) 2008 BBN Technologies Corp.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of BBN Technologies nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BBN TECHNOLOGIES AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BBN TECHNOLOGIES OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include <svnstsw/fso_is_changeable.h>

typedef struct {
    const char* search;
    _Bool match_end;
    _Bool match_previous_slash;
    const char* replace;
} search_replace_t;

static int resolve_symlink(char* buf, size_t bufsize, const char* path);
static int read_symlink(char* buf, size_t bufsize, const char* path);
static _Bool is_symlink(const char* path);
static int parent_dir(char* buf, size_t bufsize, const char* path);
static int clean_path(char* buf, size_t bufsize, const char* path);
static int get_cwd(char* buf, size_t bufsize);

_Bool
svnstsw_fso_is_changeable(const char* filename)
{
    // BASE CASE

    struct stat st;

    // get the file/directory details
    if (stat(filename, &st) == -1)
        return 1;

    // if it's owned by the user, that's a problem (since they
    // can turn on the write bit)
    if (st.st_uid == getuid())
        return 1;

    // do not check writeability if it's a directory and the sticky
    // bit is set
    if (!(S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)))
    {
        // if it's writable, that's a problem
        const int errno_backup = errno;
        if (access(filename, W_OK) == 0)
            return 1;
        if (errno != EACCES)
            return 1;
        errno = errno_backup;
    }

    // RECURSIVE CASES

    // is filename the root directory?
    if (strncmp("/", filename, 2) != 0)
    {
        // no, so get the parent directory
        char parent[parent_dir(NULL, 0, filename) + 1];
        {
            int tmp = parent_dir(parent, sizeof(parent), filename);
            if (tmp < 0)
                return 1;
            assert(tmp == (sizeof(parent) - 1));
        }

        // test the parent directory
        if (svnstsw_fso_is_changeable(parent))
            return 1;
    }

    // does the filename refer to a symbolic link?
    _Bool is_sym;
    {
        const int errno_backup = errno;
        errno = 0;
        is_sym = is_symlink(filename);
        if (errno)
            return 1;
        errno = errno_backup;
    }

    if (is_sym)
    {
        // resolve the symlink
        char resolved[resolve_symlink(NULL, 0, filename) + 1];
        {
            int tmp = resolve_symlink(resolved, sizeof(resolved), filename);
            if (tmp < 0)
                return 1;
            assert(tmp == (sizeof(resolved) - 1));
        }

        // check if the target is changeable
        if (svnstsw_fso_is_changeable(resolved))
            return 1;
    }

    return 0;
}

/**
 * @defgroup libsvnstswprvchange fso_is_changeable
 * @ingroup libsvnstswprv
 *
 * Helper functions for the implementation of
 * svnstsw_fso_is_changeable().
 *
 * @{
 */

/**
 * @brief Resolves a symlink to a absolute path name.
 *
 * The resolved path is cleaned as if passed through clean_path().
 *
 * This function is thread safe.
 *
 * @param buf Buffer of length @a bufsize that will contain the
 * symlink contents.  This may be the null pointer if @a bufsize is 0.
 * If this buffer is not big enough to hold the full symlink contents,
 * the cleaned path will be truncated (but still null-terminated) to
 * fit.
 *
 * @param bufsize Size of the buffer starting at @a buf.  This
 * function will not write more than this number of bytes beyond @a
 * buf.
 *
 * @param path Null-terminated string containing the path to the
 * symlink to read.
 *
 * @return On success, returns the length of the symlink contents.  On
 * error, returns a negative value, sets @p errno, and the contents of
 * @a buf are undefined.
 */
int
resolve_symlink(char* buf, size_t bufsize, const char* path)
{
    // clean up path if it's too ugly
    if ((!path)
        || (path[0] == '\0')
        || (path[0] != '/'))
    {
        char cp[clean_path(NULL, 0, path)];
        {
            int tmp = clean_path(cp, sizeof(cp), path);
            if (tmp < 0)
                return -1;
            assert(tmp == (sizeof(cp) - 1));
        }

        // recursive call
        return resolve_symlink(buf, bufsize, cp);
    }

    // make sure path refers to a symlink
    {
        const int errno_backup = errno;
        errno = 0;
        if (!is_symlink(path))
        {
            if (!errno)
                errno = EINVAL;
            return -1;
        }
        errno = errno_backup;
    }

    // read the symlink
    char symlink_contents[read_symlink(NULL, 0, path) + 1];
    {
        int tmp = read_symlink(symlink_contents, sizeof(symlink_contents),
                               path);
        if (read_symlink < 0)
            return -1;
        assert(tmp == (sizeof(symlink_contents) - 1));
    }

    // symlinks should never point to an empty string
    if (symlink_contents[0] == '\0')
    {
        errno = EIO;
        return -1;
    }

    // if the target is an absolute path, just clean it up and return
    // it
    if (symlink_contents[0] == '/')
        return clean_path(buf, bufsize, symlink_contents);

    // the target is a relative path.  it's relative to the parent
    // directory of the symlink, so get the symlink's parent
    char sym_parent[parent_dir(NULL, 0, path) + 1];
    {
        int tmp = parent_dir(sym_parent, sizeof(sym_parent), path);
        if (tmp < 0)
            return -1;
        assert(tmp == (sizeof(sym_parent) - 1));
    }

    // concatenate the symlink's parent directory with the relative
    // target
    char abs_contents[sizeof(sym_parent) + sizeof(symlink_contents)];
    {
        int tmp = snprintf(abs_contents, sizeof(abs_contents), "%s/%s",
                           sym_parent, symlink_contents);
        if (tmp < 0)
            return -1;
        assert(tmp == (sizeof(abs_contents) - 1));
    }

    // clean up the concatenated path and return it
    return clean_path(buf, bufsize, abs_contents);
}

/**
 * @brief Determines if @a path refers to a symlink.
 *
 * This function is thread safe.
 *
 * @param path Null-terminated string containing the path to test.
 *
 * @return Returns 1 if there is no error and @a path refers to a
 * symlink, returns 0 otherwise.  On error, @p errno is set.
 */
_Bool
is_symlink(const char* path)
{
    struct stat st;
    if (lstat(path, &st) == -1)
        return 0;

    return S_ISLNK(st.st_mode);
}

/**
 * @brief Reads the contents of a symlink.
 *
 * This function is thread safe.
 *
 * @param buf Buffer of length @a bufsize that will contain the
 * symlink contents.  This may be the null pointer if @a bufsize is 0.
 * If this buffer is not big enough to hold the full symlink contents,
 * the cleaned path will be truncated (but still null-terminated) to
 * fit.
 *
 * @param bufsize Size of the buffer starting at @a buf.  This
 * function will not write more than this number of bytes beyond @a
 * buf.
 *
 * @param path Null-terminated string containing the path to the
 * symlink to read.
 *
 * @return On success, returns the length of the symlink contents.  On
 * error, returns a negative value, sets @p errno, and the contents of
 * @a buf are undefined.
 */
int
read_symlink(char* buf, size_t bufsize, const char* path)
{
    // make sure path really is a symlink
    const int errno_backup = errno;
    errno = 0;
    if (!is_symlink(path))
    {
        if (!errno)
            errno = EINVAL;
        return -1;
    }
    errno = errno_backup;

    // size for the temporary buffer that will hold the contents of
    // the symlink
    size_t len = (bufsize > 1024) ? bufsize : 1024;

    // keep trying bigger and bigger buffers until everything can fit
    while (1)
    {
        // allocate a temporary buffer
        char tmp[len];

        // fill the buffer with the contents of the symlink
        ssize_t written = readlink(path, tmp, len);

        // was the buffer big enough?
        if (written < len)
        {
            // there was an error reading the link
            if (written == -1)
                return -1;

            // buffer was big enough.  readlink() doesn't
            // null-terminate, so we must do that.
            tmp[written] = '\0';

            // copy the results to the user's buffer
            return snprintf(buf, bufsize, "%s", tmp);
        }

        // buffer wasn't big enough -- try again with a bigger buffer
        len *= 2;
    }
    // shouldn't be possible to get here
    abort();
}

/**
 * @brief Determines the parent directory of @a path.
 *
 * The results are clean (as if passed through clean_path()).
 *
 * This function is thread safe.
 *
 * @param buf Buffer of length @a bufsize that will contain the parent
 * directory.  This may be the null pointer if @a bufsize is 0.  If
 * this buffer is not big enough to hold the full parent directory,
 * the cleaned path will be truncated (but still null-terminated) to
 * fit.
 *
 * @param bufsize Size of the buffer starting at @a buf.  This
 * function will not write more than this number of bytes beyond @a
 * buf.
 *
 * @param path Null-terminated string containing the path whose parent
 * should be placed in @a buf.
 *
 * @return On success, returns the length of the parent directory.  On
 * error, returns a negative value, sets @p errno, and the contents of
 * @a buf are undefined.
 */
int
parent_dir(char* buf, size_t bufsize, const char* path)
{
    // clean path
    char cp[clean_path(NULL, 0, path) + 1];
    {
        int tmp = clean_path(cp, sizeof(cp), path);
        if (tmp < 0)
            return -1;
        assert(tmp == (sizeof(cp) - 1));
    }

    // find the last slash
    char* found = strrchr(cp, '/');
    assert(found);

    // terminate the string at or just after the slash
    if (found == cp)
        found[1] = '\0';
    else
        found[0] = '\0';

    // copy the string to the user's buffer
    return snprintf(buf, bufsize, cp);
}

/**
 * @brief Takes @a path and converts it to an absolute path with no "."
 * or ".." components.
 *
 * This function is like realpath() except this doesn't resolve
 * symbolic links.
 *
 * If @a path is a relative path, it is converted to an absolute path
 * by prepending the results of calling get_cwd().
 *
 * This function is thread safe.
 *
 * @param buf Buffer of length @a bufsize that will contain the
 * cleaned-up path.  This may be the null pointer if @a bufsize is 0.
 * If this buffer is not big enough to hold the full cleaned path, the
 * cleaned path will be truncated (but still null-terminated) to fit.
 *
 * @param bufsize Size of the buffer starting at @a buf.  This
 * function will not write more than this number of bytes beyond @a
 * buf.
 *
 * @param path Null-terminated string containing the path to clean up.
 *
 * @return On success, returns the length of the cleaned path.  On
 * error, returns a negative value, sets @p errno, and the contents of
 * @a buf are undefined.
 */
int
clean_path(char* buf, size_t bufsize, const char* path)
{
    // we don't like null or empty strings
    if ((!path) || (path[0] == '\0'))
    {
        errno = EINVAL;
        return -1;
    }

    // make sure it's an absolute path
    if (path[0] != '/')
    {
        // get the current working directory
        char cwd[get_cwd(NULL, 0) + 1];
        {
            int tmp = get_cwd(cwd, sizeof(cwd));
            if (tmp < 0)
                return -1;
            assert(tmp == (sizeof(cwd) - 1));
        }

        // append path to the working directory
        char abs_path[strlen(cwd) + 1 + strlen(path) + 1];
        {
            int tmp = snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, path);
            if (tmp < 0)
                return -1;
            assert(tmp == (sizeof(abs_path) - 1));
        }

        // recurse
        return clean_path(buf, bufsize, abs_path);
    }

    // size of the buffer to hold the cleaned-up path.  Since cleaning
    // never causes the length of the path to increase, strlen(path)
    // should always be big enough.
    const size_t pathbuflen = strlen(path) + 1;

    // buffer that will hold the final cleaned result.  it starts off
    // as path and will get progressively cleaner until we're done.
    char fixed[pathbuflen];
    if (snprintf(fixed, pathbuflen, "%s", path) < 0)
        return -1;

    // buffer to hold a working copy of the cleaned result.  this will
    // be hacked until an incremental cleaning stage is complete.
    char working[pathbuflen];
    if (snprintf(working, pathbuflen, "%s", path) < 0)
        return -1;

    // search and replace directives to clean the path.
    const search_replace_t sr[] = {
        {"/./",  0, 0, "/"}, // any "/./" can be compressed to "/"
        {"/.",   1, 0, ""},  // remove "/." at end of path
        {"//",   0, 0, "/"}, // compress any consecutive slashes to a single
        {"/",    1, 0, ""},  // remove trailing slashes
        {"/../", 0, 1, "/"}, // compress "/foo/../" to "/"
        {"/..",  1, 1, ""},  // compress trailing "/foo/.." to ""
        {NULL,   0, 0, NULL}
    };

    // perform all of the above search and replaces
    for (size_t i = 0; sr[i].search; ++i)
    {
        // save the number of characters in the search string
        const size_t searchlen = strlen(sr[i].search);

        // the search and replace algorithm only works if we're not
        // searching for the empty string and the replace text is
        // shorter than the search text (otherwise we'd have to deal
        // with buffers not being big enough)
        assert(searchlen && (searchlen >= strlen(sr[i].replace)));

        // where to start looking for the search string (defaults to
        // the beginning of the path)
        char* searchstart = working;

        // where the search string was found
        char* found = NULL;

        // repeatedly search the string, replacing all occurrences of
        // the search string with the replace string
        while ((found = strstr(searchstart, sr[i].search)) != NULL)
        {
            // if we must match the end of the path and we're not yet
            // at the end, continue the search
            if (sr[i].match_end)
            {
                // are we at the end yet?
                if (found[searchlen] != '\0')
                {
                    // nope.  find the next match.
                    searchstart = found + 1;
                    continue;
                }
                else
                {
                    // we're at the end.  by setting searchstart to
                    // working, we'll repeat the search from the
                    // beginning after the replacement happens.  this
                    // is in case the replacement would result in
                    // another ending match
                    searchstart = working;
                }
            }

            // size of the block of matching text that will be
            // replaced.  by default, this equals the length of the
            // search string
            size_t matchlen = searchlen;

            // do we need to include everything after the previous
            // slash?
            if (sr[i].match_previous_slash)
            {
                // terminate the string to make it easy to find the
                // previous slash
                found[0] = '\0';

                // locate the previous slash
                char* found_sl = strrchr(working, '/');

                // was there a previous slash?
                if (found_sl)
                {
                    // update the length of the match to include the
                    // number of characters at and after the previous
                    // slash
                    matchlen += (found - found_sl);

                    // the block of text to remove begins at the
                    // previous slash
                    found = found_sl;
                }
            }

            // the number of bytes into the path where the match begins
            int offset = found - working;
            assert((offset >= 0) && (offset < (pathbuflen - matchlen)));

            // perform the replace
            if (snprintf(found, pathbuflen - offset, "%s%s", sr[i].replace,
                         fixed + offset + matchlen) < 0)
                return -1;

            // update the fixed buffer
            if (snprintf(fixed, pathbuflen, "%s", working) < 0)
                return -1;
        }
    }
    // path is completely cleaned.  put the results in the caller's
    // buffer.
    return snprintf(buf, bufsize, "%s", fixed);
}

/**
 * @brief Gets the current working directory.
 *
 * This function is thread safe.
 *
 * @param buf Buffer of length @a bufsize that will contain the
 * current working directory.  This may be the null pointer if @a
 * bufsize is 0.  If this buffer is not big enough to hold the full
 * working directory, the working directory will be truncated (but
 * still null-terminated) to fit.
 *
 * @param bufsize Size of the buffer starting at @a buf.  This
 * function will not write more than this number of bytes beyond @a
 * buf.
 *
 * @return On success, returns the length of the current working
 * directory.  On error, returns a negative value, sets @p errno, and
 * the contents of @a buf are undefined.
 */
int
get_cwd(char* buf, size_t bufsize)
{
    const int errno_backup = errno;

    // size of the temporary buffer to allocate
    size_t cwdlen = (bufsize > 1024) ? bufsize : 1024;

    // keep trying to get the current working directory until we have
    // allocated a big enough buffer to hold the whole thing.
    while (1)
    {
        // allocate the buffer
        char tmp[cwdlen];

        // fill the buffer with the cwd
        const char* d = getcwd(tmp, sizeof(tmp));

        // was getcwd() successful?
        if (d)
            return snprintf(buf, bufsize, "%s", tmp);

        // not successful -- was the problem something other than the
        // buffer being too small?
        if (errno != ERANGE)
            return -1;

        // buffer was too small.  restore errno since we haven't
        // failed yet.
        errno = errno_backup;

        // double the size of the buffer
        cwdlen *= 2;
    }
    // shouldn't be possible to get here.
    abort();
}

/**
 * @}
 */
