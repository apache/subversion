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

#ifndef SVNSTSW_FSO_IS_CHANGEABLE_H
#define SVNSTSW_FSO_IS_CHANGEABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup libsvnstswpub
 *
 * @brief Tests if the file or directory referred to by @a path is
 * changeable.
 *
 * A file or directory is considered changeable if it can be directly
 * modified or if any component of the path can be renamed.  If any
 * component of the path refers to a symbolic link, the target is also
 * tested.  Permissions are tested against the real user and group
 * IDs, not the effective IDs.
 *
 * This function is designed to help determine whether a file is safe
 * to exec from a setuid/setgid binary.  If this function returns
 * true, the user may be able to start a shell as the effective user.
 *
 * This function is thread-safe if the user's C POSIX library is
 * thread-safe.
 *
 * Defined in @p svnstsw/fso_is_changeable.h which is included in @p
 * svnstsw/svnstsw.h
 *
 * @param path Null-terminated string containing the path to the file
 * or directory to test.
 *
 * @return On success, returns 1 if the user (real user and group IDs)
 * can change the file or directory referred to by @a path, or 0 if
 * the file or directory can not be changed.  On error, returns 1 and
 * sets @a errno.  Error conditions and @a errno values are described
 * in the specifications for stat(), access(), snprintf(), lstat(),
 * readlink(), and getcwd().
 */
_Bool svnstsw_fso_is_changeable(const char* path);

#ifdef __cplusplus
}
#endif

#endif
