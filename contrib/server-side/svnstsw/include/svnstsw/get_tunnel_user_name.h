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

#ifndef SVNSTSW_GET_TUNNEL_USER_NAME_H
#define SVNSTSW_GET_TUNNEL_USER_NAME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup libsvnstswpub
 *
 * @brief Fetch the username that should be passed to svnserve via the
 * @p --tunnel-user argument.
 *
 * This function uses the getlogin_r() function to fetch the username
 * of the user that invoked this process.  If getlogin_r() is
 * unavailable or returns an error, getpwuid_r() is used instead.
 * Note that if multiple users share the same UID, this function may
 * return the wrong username.
 *
 * This function is thread-safe if the user's C POSIX library is
 * thread-safe.
 *
 * Defined in svnstsw/get_tunnel_user_name.h which is included in
 * svnstsw/svnstsw.h
 *
 * This function uses getlogin_r() to determine the login name.  Since
 * users might be able to trick getlogin_r() into returning an
 * arbitrary username on some systems, the results are verified using
 * getpwnam_r().  This verification does not prevent a user from
 * imitating another user if the two users share the same user ID.  If
 * verification fails, or if getlogin_r() fails for any reason,
 * getpwuid_r() is used instead.
 *
 * This function may return the incorrect username if multiple users
 * share the same user ID.
 *
 * @param buf Pointer to a buffer of length @a bufsize where the
 * null-terminated username will be written.  This may be the null
 * pointer if @a bufsize is 0.  This function will not write beyond
 * the end of the buffer (@a buf + @a bufsize - 1).
 *
 * @param bufsize Size of the buffer at @a buf.  If 0, @a buf will not
 * be written to and may be the null pointer.  If the buffer size is
 * less than the length of the null-terminated login name, then @a buf
 * will be filled with a truncated, null-terminated version of the
 * login name.
 *
 * @return On success, returns the length of the username (not
 * including the null terminator) to use with the @p --tunnel-user
 * argument to @p svnserve.  Thus, if the return value is less than @a
 * bufsize, the buffer at @a buf contains the full username.  If the
 * return value is greater than or equal to @a bufsize, the username
 * was truncated to fit in @a buf.  If there is an error, a negative
 * value is returned and @p errno is set.  An error value of @c EINVAL
 * indicates that no username is associated with the UID of the
 * invoking user.  All other error values and conditions are described
 * in the specifications for getpwuid_r() and snprintf().
 *
 * @sa get_login_name(), get_user_name()
 */
int svnstsw_get_tunnel_user_name(char* buf, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif
