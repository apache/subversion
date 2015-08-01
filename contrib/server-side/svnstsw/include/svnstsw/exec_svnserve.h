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

#ifndef SVNSTSW_EXEC_SVNSERVE_H
#define SVNSTSW_EXEC_SVNSERVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup libsvnstswpub
 *
 * @brief Execute @p svnserve in tunnel mode with the tunnel user set
 * to @a tunnel_user and the virtual repository root path set to @a
 * svn_root.
 *
 * Specifically, the executable named by @a svnserve_path is executed
 * (see execve()) with the environment set to @a envp and with
 * arguments <tt>--root=</tt>@a svn_root, @p --tunnel, and
 * <tt>--tunnel-user=</tt>@a tunnel_user appended to the arguments
 * given in @a argv.
 *
 * This function is thread-safe if the user's C POSIX library is
 * thread-safe.
 *
 * Defined in svnstsw/exec_svnserve.h which is included in
 * svnstsw/svnstsw.h
 *
 * Executables using this function are expected to be installed with
 * either the setuid or the setgid bit set.  Because of this, there
 * are a few recommendations:
 * - The executable named by the @a svnserve_path argument should not
 *   be a shell script because of numerous well-known attacks via
 *   specially-crafted environment variables and arguments.
 * - The @a envp argument should be empty (either the null pointer or
 *   an array containing only a null terminator).  This is especially
 *   true if the executable named by the @a svnserve_path argument is
 *   a shell script.
 * - @a svnserve_path and @a svn_root should be passed through
 *   svnstsw_fso_is_changeable() to make sure that neither they nor
 *   their parent directories are writable by the user.
 *
 * @param svnserve_path Null-terminated string containing the path to
 * the @p svnserve executable.  This must be an absolute path and must
 * refer to an existing executable file.  This parameter must not be
 * the null pointer.  Callers are encouraged to use
 * svnstsw_fso_is_changeable() to check the safety of using the path
 * before calling this function.
 *
 * @param svn_root Null-terminated string containing the repository
 * virtual root path, passed to @p svnserve via its @p --root
 * command-line parameter.  If this parameter is the null pointer or
 * an empty string, the root directory ('/') is used.  The path must
 * be an absolute path, must exist, and must refer to a directory.
 * Callers are encouraged to use svnstsw_fso_is_changeable() to check
 * the safety of using the path before calling this function.
 *
 * @param tunnel_user Null-terminated string containing the Subversion
 * username, passed to @p svnserve via its @p --tunnel-user
 * command-line parameter.  This parameter must not be the null
 * pointer or an empty string.  Callers are encouraged to use the
 * string returned by svnstsw_get_tunnel_user_name() for this
 * parameter.
 *
 * @param argv Null-terminated array of null-terminated strings to use
 * as the first arguments to the @p svnserve executable.  Note that
 * convention dictates that @a argv[0] must match @a svnserve_path.
 * If this parameter is the null pointer, it is the equivalent to
 * passing in a two-element array consisting of @a svnserve_path and a
 * null terminator.  The <tt>--root=</tt>@a svn_root, @p --tunnel, and
 * <tt>--tunnel-user=</tt>@a tunnel_user arguments will be appended to
 * the arguments in @a argv before being passed to the executable
 * named by @a svnserve_path.
 *
 * @param envp Null-terminated array of null-terminated strings
 * containing the desired environment for the @p svnserve process.  If
 * this parameter is the null pointer, it is the equivalent to passing
 * in an array consisting of only a null terminator.  For security
 * reasons, it is recommended that callers pass in an empty
 * environment.  By convention, each string in this array should be in
 * the form of <i>name</i><tt>=</tt><i>value</i>.  For security
 * reasons, the contents of @a envp might not be passed as-is to the
 * executable named by @a svnserve_path.
 *
 * @return Does not return on success.  On failure, returns a negative
 * value and sets @p errno.  Error conditions and @p errno values are
 * described in the specifications for stat(), snprintf(), and
 * execve(), with the addition that @c EINVAL may indicate an invalid
 * parameter.
 *
 * @sa svnstsw_get_tunnel_user_name(), svnstsw_fso_is_changeable()
 */
int svnstsw_exec_svnserve(const char* svnserve_path, const char* svn_root,
                          const char* tunnel_user,  const char* const* argv,
                          const char* const* envp);

#ifdef __cplusplus
}
#endif

#endif
