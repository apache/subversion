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

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

#include <svnstsw/get_tunnel_user_name.h>

static int get_login_name(char* buf, size_t bufsize);
static _Bool is_login_name_valid(const char* login, uid_t uid);
static int get_user_name(char* buf, size_t bufsize);

int
svnstsw_get_tunnel_user_name(char* buf, size_t bufsize)
{
    // first try get_login_name(), saving errno in case it fails
    const int errno_backup = errno;
    int tmp = get_login_name(buf, bufsize);
    if (tmp != -1)
        return tmp;

    // since get_login_name() failed, restore errno and try
    // get_user_name()
    errno = errno_backup;
    return get_user_name(buf, bufsize);
}

/**
 * @defgroup libsvnstswprvusername get_tunnel_user_name
 * @ingroup libsvnstswprv
 *
 * Helper functions for the implementation of
 * svnstsw_get_tunnel_user_name().
 *
 * @{
 */

/**
 * @brief Fetch the login name of the user who invoked this process.
 *
 * This function uses getlogin_r() to obtain the login name.  The
 * login name is verified by comparing the current UID with the UID
 * associated with the login name.
 *
 * This function is thread safe.
 *
 * @param buf the buffer to fill with the null-terminated login name.
 * This may be the null pointer if @a bufsize is 0.
 *
 * @param bufsize the size of the buffer at @a buf.  This may be 0.
 * If the buffer size is less than the length of the null-terminated
 * login name, then @a buf will be filled with a truncated,
 * null-terminated version of the login name.
 *
 * @return Returns the length of the login name (not including the
 * null terminator).  If there is an error, a negative value is
 * returned and @p errno is set.  Error values and conditions are
 * described in the specifications for getlogin_r() and
 * get_login_name() with the addition that @p ENXIO may also indicate
 * that the login name returned by the operating system could not be
 * verified to be correct.
 *
 * @sa is_login_name_valid()
 */
int
get_login_name(char* buf, size_t bufsize)
{
    // we need to create a buffer to hold the results of getlogin_r().
    // figure out how much buffer to allocate using sysconf.
    const int errno_backup = errno;
    const long lenmax = sysconf(_SC_LOGIN_NAME_MAX);

    // restore errno in case there was a problem with sysconf
    errno = errno_backup;

    // initial guess at a buffer size large enough to hold the login
    // name
    size_t len = (lenmax > 0) ? lenmax : (bufsize > 16) ? bufsize : 16;

    // keep trying to fetch the login name until we've allocated
    // enough buffer space to hold the results
    while (1)
    {
        // create a temporary buffer
        char login[len];

        // fill the buffer with the login name.
        int err = getlogin_r(login, sizeof(login));

        // if getlogin() returned without error, verify the returned
        // username, copy the string to the user-supplied buffer, and
        // return
        if (!err)
        {
            // reset errno so that we can tell the difference between
            // an invalid login name and an error during verification.
            errno = 0;

            // apparently getlogin() is not trustworthy, so call
            // is_login_name_valid() to verify the login name returned
            // by getlogin().
            if (is_login_name_valid(login, getuid())) {

                // restore errno since there was no error
                errno = errno_backup;

                // fill the user's buffer with the login name and
                // return
                return snprintf(buf, bufsize, "%s", login);
            }

            // if the UIDs didn't match, set errno to ENXIO to
            // indicate that we couldn't get the login name.
            // Otherwise, leave errno alone (errno will be set to
            // whatever is_login_name_valid() set it to)
            if (!errno)
                errno = ENXIO;

            return -1;
        }

        // getlogin_r() returned an error.  return unless the login
        // name is too big to fit in our temporary buffer
        if (err != ERANGE)
        {
            errno = err;
            return -1;
        }

        // adjust the size of the buffer and try again
        len *= 2;
    }

    // should not be possible to get here
    abort();
}

/**
 * @brief Verify that the user with the username given by @a login has
 * a UID matching @a uid.
 *
 * This function is thread safe.
 *
 * @param login the username to verify against @a uid.
 *
 * @param uid the UID that should match the UID associated with the
 * user with the username given in @a login.
 *
 * @return Returns 1 if there were no errors encountered while
 * fetching the passwd details for the user specified by @a login and
 * the UID associated with the user equals @a uid.  Returns 0
 * otherwise.  If an error was encountered, @p errno will be set
 * appropriately.  To differentiate between an error and a UID
 * mismatch, set @p errno to 0 before calling and test its value after
 * this function returns.  Error values and conditions are described
 * in the specification for getpwnam_r().
 */
_Bool
is_login_name_valid(const char* login, uid_t uid)
{
    // we need to create a buffer to hold the results of
    // getpwnam_r().  We want to guess a good size for the
    // buffer, so use sysconf() to figure out how much to
    // allocate.
    const int errno_backup = errno;
    const long lenmax = sysconf(_SC_GETPW_R_SIZE_MAX);

    // restore errno in case sysconf() returned an error
    errno = errno_backup;

    // the initial guess for the size of the passwd buffer.
    size_t len = (lenmax > 0) ? lenmax : 64;

    // Get the UID associated with the username.  If the
    // buffer turns out to be too small, we'll adjust and
    // try again.
    while(1)
    {
        // variables to hold the results of getpwnam_r()
        char pwdbuf[len];
        struct passwd pwd;
        struct passwd* pwd_p = 0;

        // fill in the passwd structure so that we can get
        // the UID
        int err = getpwnam_r(login, &pwd, pwdbuf,
                             sizeof(pwdbuf), &pwd_p);

        // did getpwnam_r() succeed?
        if (!err)
        {
            // does the UID associated with the getlogin()
            // username match the current username?
            if (pwd_p && (pwd.pw_uid == uid))
                return 1;

            // Either the passwd entry was not found or
            // there was a mismatch (perhaps the user
            // tricked getlogin() into returning someone
            // else's username).
            return 0;
        }

        // getpwnam_r() failed.  return unless the failure
        // was caused by the buffer being too small.
        if (err != ERANGE)
        {
            errno = err;
            return 0;
        }

        // buffer was too small.  adjust and try again.
        len *= 2;
    }

    // should not be possible to get here
    abort();
}

/**
 * @brief Fetch a login name associated with the UID of the account
 * used to invoked this process.
 *
 * This function is thread safe.
 *
 * @param buf the buffer to fill with the null-terminated login name.
 * This may be the null pointer if @a bufsize is 0.
 *
 * @param bufsize the size of the buffer at @a buf.  This may be 0.
 * If the buffer size is less than the length of the null-terminated
 * login name, then @a buf will be filled with a truncated,
 * null-terminated version of the login name.
 *
 * @return the length of the login name (not including the null
 * terminator).  If there is an error, a negative value is returned
 * and @p errno is set.  An error value of @p EINVAL may indicate that
 * no username is associated with the UID of the invoking user.  All
 * other error values and conditions are described in the
 * specification for getpwuid_r().
 */
int
get_user_name(char* buf, size_t bufsize)
{
    // figure out how much buffer to allocate
    const int errno_backup = errno;
    const long lenmax = sysconf(_SC_GETPW_R_SIZE_MAX);
    errno = errno_backup;

    // initial guess at a buffer size large enough to hold the passwd
    // details
    size_t len = (lenmax > 0) ? lenmax : (bufsize > 64) ? bufsize : 64;

    // keep trying to fetch account details until we've allocated
    // enough buffer space to hold the results
    while (1)
    {
        // create a temporary buffer
        char pwdbuf[len];

        // fetch the account details
        struct passwd pwd;
        struct passwd* pwd_p = 0;
        int err = getpwuid_r(getuid(), &pwd, pwdbuf, sizeof(pwdbuf), &pwd_p);

        // did getpwuid_r() return without error?
        if (!err)
        {
            // did getpwuid_r() find the user?  if so, copy the
            // user's login name to the user-supplied buffer and
            // return
            if (pwd_p)
                return snprintf(buf, bufsize, "%s", pwd.pw_name);

            // user not found
            errno = EINVAL;
            return -1;
        }

        // getpwuid_r() returned an error.  return unless the buffer
        // wasn't big enough.
        if (err != ERANGE)
        {
            errno = err;
            return -1;
        }

        // adjust the size of the buffer and try again
        len *= 2;
    }

    // should not be possible to get here
    abort();
}

/**
 * @}
 */
