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

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <svnstsw/exec_svnserve.h>

static _Bool is_svnserve_path_valid(const char* svnserve_path);
static _Bool is_svn_root_valid(const char* svn_root);
static _Bool is_tunnel_user_valid(const char* tunnel_user);

int
svnstsw_exec_svnserve(const char* svnserve_path, const char* svn_root,
                      const char* tunnel_user, const char* const* argv,
                      const char* const* envp)
{
    //////////////////////
    // path to svnserve //
    //////////////////////

    // make sure the path is valid
    if (!is_svnserve_path_valid(svnserve_path))
        return -1;

    /////////////////////
    // repository root //
    /////////////////////

    // use the default path if applicable
    if ((!svn_root) || (svn_root[0] == '\0'))
        svn_root = "/";

    // make sure the path is valid
    if (!is_svn_root_valid(svn_root))
        return -1;

    // create a buffer for generating the --root argument
    char root_param[strlen("--root=") + strlen(svn_root) + 1];

    // generate the --root argument using svn_root
    {
        int tmp = snprintf(root_param, sizeof(root_param), "--root=%s",
                           svn_root);
        // make sure that we chose the correct size for the buffer
        assert(tmp == (sizeof(root_param) - 1));
    }

    /////////////////
    // tunnel user //
    /////////////////

    // make sure the tunnel user is valid
    if (!is_tunnel_user_valid(tunnel_user))
        return -1;

    // create a buffer for generating the --tunnel-user argument
    char tunnel_user_param[strlen("--tunnel-user=")
                           + strlen(tunnel_user) + 1];

    // generate the --tunnel-user argument using the user's login name
    {
        int tmp = snprintf(tunnel_user_param, sizeof(tunnel_user_param),
                           "--tunnel-user=%s", tunnel_user);
        // make sure that we chose the correct size for the buffer
        assert(tmp == (sizeof(tunnel_user_param) - 1));
    }

    ////////////////////////
    // generate arguments //
    ////////////////////////

    // figure out how many arguments we are going to pass to svnserve
    size_t argc;
    if (!argv)
    {
        // arg 0 is the path to the svnserve binary, args 1-3 are the
        // --root, --tunnel, and --tunnel-user parameters.
        argc = 4;
    }
    else
    {
        // count the number of arguments in the given argv vector
        argc = 0;
        while (argv[argc])
            ++argc;

        // we'll tack on the --root, --tunnel, and --tunnel-user
        // parameters after the parameters given in argv.
        argc += 3;
    }

    // create an array to hold the arguments (the "+1" is for the null
    // terminator)
    const char* svnserve_argv[argc + 1];

    // fill in the arguments
    {
        size_t i = 0;
        if (!argv)
            svnserve_argv[i++] = svnserve_path;
        else
            for (; argv[i]; ++i)
                svnserve_argv[i] = argv[i];

        svnserve_argv[i++] = root_param;
        svnserve_argv[i++] = "--tunnel";
        svnserve_argv[i++] = tunnel_user_param;
        svnserve_argv[i] = NULL;
        assert(i == argc);
    }

    //////////////////////////
    // generate environment //
    //////////////////////////

    // make sure we have a valid envp
    const char* const svnserve_envp_default[] = {NULL};
    const char* const* svnserve_envp = svnserve_envp_default;
    if (envp)
        svnserve_envp = envp;

    ///////////////
    // call exec //
    ///////////////

/*
    // debug output
    fprintf(stderr, "svnserve = %s\n", svnserve_path);
    for (int i = 0; svnserve_argv[i]; ++i)
    {
        fprintf(stderr, "Arg[%i] = %s\n", i, svnserve_argv[i]);
    }
    for (int i = 0; svnserve_envp[i]; ++i)
    {
        fprintf(stderr, "Env[%i] = %s\n", i, svnserve_envp[i]);
    }
*/

    // call execve().  If execve() fails, we return -1 so that the
    // caller knows to look at errno.
    //
    // Unfortunately, argv and envp have to be cast to strip off the
    // first const -- see the discussion in the 'rationale' section of
    // the execve() specification in POSIX.1-2004.  This cast should
    // be safe; the specification says, "The argv[] and envp[] arrays
    // of pointers and the strings to which those arrays point shall
    // not be modified by a call to one of the exec functions, except
    // as a consequence of replacing the process image."
    //
    // Note that exec does not modify the real or effective user ID
    // unless svnserve_path refers to an executable with the SUID bit
    // set.  This means that svnserve's privileges will be the union
    // of the real user's privileges and the effective user's
    // privileges.  It is not possible to limit svnserve's privileges
    // to just those of the effective user by calling
    // setuid(geteuid()) before exec, because setuid() does not change
    // the real UID without superuser privileges.  The only way to
    // shed the real user's privileges is to give this wrapper
    // superuser privileges (set the wrapper's owner to root and
    // enable the SUID bit) and call setuid() with the target user's
    // UID before calling exec.  I don't think this extra effort would
    // provide any substantial gain, and it could open the possibility
    // of a malicious user gaining superuser privileges.
    //
    return execve(svnserve_path, (char* const*)svnserve_argv,
                  (char* const*)svnserve_envp);
}

/**
 * @defgroup libsvnstswprvexec exec_svnserve
 * @ingroup libsvnstswprv
 *
 * Helper functions for the implementation of svnstsw_exec_svnserve().
 *
 * @{
 */

/**
 * @brief Makes sure @a svnserve_path is an absolute path and refers
 * to an existing regular file.
 *
 * @param svnserve_path Null-terminated string containing the path to
 * check.
 *
 * @return Returns 1 if there is no error, the path is absolute, and
 * the path refers to an existing regular file.  Otherwise, sets @p
 * errno and returns 0.
 */
_Bool
is_svnserve_path_valid(const char* svnserve_path)
{
    // make sure we were given an absolute path
    if ((!svnserve_path) || (svnserve_path[0] != '/'))
    {
        errno = EINVAL;
        return 0;
    }

    // fetch the file details.  Note that stat() follows symlinks
    struct stat st;
    if (stat(svnserve_path, &st) == -1)
        return 0;

    // is it not a normal file?
    if (!S_ISREG(st.st_mode))
    {
        errno = EINVAL;
        return 0;
    }

    return 1;
}

/**
 * @brief Makes sure @a svn_root is an absolute path and refers
 * to an existing directory.
 *
 * @param svn_root Null-terminated string containing the path to
 * check.
 *
 * @return Returns 1 if there is no error, the path is absolute, and
 * the path refers to an existing directory.  Otherwise, sets @p errno
 * and returns 0.
 */
_Bool
is_svn_root_valid(const char* svn_root)
{
    // make sure the path is absolute
    if ((!svn_root) || (svn_root[0] != '/'))
    {
        errno = EINVAL;
        return 0;
    }

    // fetch the directory's details.  Note that stat() follows
    // symlinks
    struct stat st;
    if (stat(svn_root, &st) == -1)
        return 0;

    // make sure it is a directory
    if (!S_ISDIR(st.st_mode))
    {
        errno = EINVAL;
        return 0;
    }

    return 1;
}

/**
 * @brief Tests @a tunnel_user to make sure it is a valid @p svnserve
 * tunnel user name.
 *
 * Currently just tests whether @a tunnel_user is neither the null
 * pointer nor the empty string.
 *
 * @param tunnel_user Null-terminated string containing the user name
 * to check.
 *
 * @return Returns 1 if there is no error and @a tunnel_user is
 * neither the null pointer nor the empty string.  Otherwise, sets @p
 * errno and returns 0.
 * set.
 */
_Bool
is_tunnel_user_valid(const char* tunnel_user)
{
    if ((!tunnel_user) || (tunnel_user[0] == '\0'))
    {
        errno = EINVAL;
        return 0;
    }

    return 1;
}

/**
 * @}
 */
