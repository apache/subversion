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
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>

#include <apr_general.h>  // for apr_app_initialize()
#include <apr_pools.h>    // for apr_pool_create_ex()
#include <apr_getopt.h>   // for apr_getopt_long() and friends

#include <svnstsw/svnstsw.h>

#if ! (defined (SVNSERVE) && defined (SVNSERVE_DEFAULT_ROOT) && \
       defined (ALLOW_SVNSERVE_ROOT_OVERRIDE) && defined(SVNSTSW_UMASK))
#  error required macro not #defined
#endif

/**
 * @defgroup svnstsw svnstsw executable
 *
 * @brief Simple executable that uses the @ref libsvnstsw to start @p
 * svnserve in tunnel mode.
 *
 * The path to @p svnserve is set at compile-time and cannot be
 * changed.  The @p -R and @p --read-only command-line arguments are
 * honored.  The @p -r and @p --root command-line arguments can be
 * optionally honored via a compile-time setting.  All other arguments
 * are ignored (if they apply when @p svnserve is in tunnel mode) or
 * considered invalid (if they do not apply when @p svnserve is in
 * tunnel mode).  See the manual page (svnstsw(8)) for details.
 *
 * @{
 */

///////////////////////////
// COMPILE-TIME SETTINGS //
///////////////////////////

/**
 * @brief Path to the @p svnserve executable.
 *
 * The value of this variable is set by @p configure at build time.
 */
static const char* const Svnserve = SVNSERVE;

/**
 * @brief Default Subversion repository virtual root path.
 *
 * The value of this variable is set by @p configure at build time.
 */
static const char* const Svnserve_default_root = SVNSERVE_DEFAULT_ROOT;

/**
 * @brief Whether the @p --root or @p -r command-line arguments are
 * honored.
 *
 * If true, the repository virtual root passed to
 * svnstsw_exec_svnserve() can be changed via the @p --root or @p -r
 * command-line arguments.  If false, the @p --root and @p -r
 * command-line arguments are ignored and #Svnserve_default_root is
 * always passed to svnstsw_exec_svnserve().
 *
 * The value of this variable is set by @p configure at build time.
 */
static const _Bool Allow_svnserve_root_override = ALLOW_SVNSERVE_ROOT_OVERRIDE;

/**
 * @brief File mode creation mask to apply to new files and
 * directories.
 *
 * The value of this variable is set by @p configure at build time.
 *
 * @sa umask(), <sys/stat.h>
 */
static const mode_t Svnstsw_umask = SVNSTSW_UMASK();

///////////////////////////////////
// COMMAND-LINE ARGUMENT PARSING //
///////////////////////////////////

/**
 * @brief Structure holding the values passed in via the command-line
 * arguments.
 */
typedef struct {
    /**
     * @brief whether @p svnserve should be passed the @p --read-only
     * command-line argument
     */
    _Bool read_only;

    /**
     * @brief the path that should be passed to @p svnserve via the @p
     * --root command-line argument
     */
    const char* root;
} svnstsw_args_t;

/**
 * @brief Option code for the @p --tunnel-user command-line argument.
 */
#define SVNSERVE_OPT_TUNNEL_USER 259

/**
 * @brief Option code for the @p --version command-line argument.
 */
#define SVNSERVE_OPT_VERSION 260

/**
 * @brief Option codes and descriptions for svnstsw's valid
 * command-line arguments.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * APR requires that options without abbreviations have codes greater
 * than 255.
 */
static const apr_getopt_option_t options[] =
{
    {"help",             'h', 0, "display this help"},
    {"version",          SVNSERVE_OPT_VERSION, 0, "show program version "
                                                  "information"},
    {"read-only",        'R', 0, "force read only, overriding repository "
                                 "config file"},
    {"root",             'r', 1, (ALLOW_SVNSERVE_ROOT_OVERRIDE)
                                 ? "root of directory to serve" : "ignored"},
    {"tunnel",           't', 0, "ignored"},
    {"tunnel-user",      SVNSERVE_OPT_TUNNEL_USER, 1, "ignored"},
    {0,                  0,   0, 0}
};

///////////////////////////
// FUNCTION DECLARATIONS //
///////////////////////////

int main(int argc, const char* const* argv, const char* const* env);
static void parse_args(svnstsw_args_t* args_p, int* argc_p,
                       const char* const** argv_p, const char* const** env_p);
static void print_usage(const char* argv0, FILE* out);
static void print_executable_and_path(FILE* out);
static _Bool is_equivalent_file(const char* file1, const char* file2);
static apr_pool_t* initialize_apr(int* argc_p, const char* const** argv_p,
                                  const char* const** env_p);
static int abort_on_pool_failure(int retcode);

//////////////////////////
// FUNCTION DEFINITIONS //
//////////////////////////

/**
 * @brief Executable entry.
 *
 * @param argc length of @a argv (not including the null terminator)
 *
 * @param argv null-terminated array of null-terminated strings
 * containing the command-line arguments to this executable (where
 * @a argv[0] is the path to the executable)
 *
 * @param env null-terminated array of null-terminated strings
 * containing the environment.
 *
 * @return Does not return on success.  Returns a non-zero value on
 * error.
 */
int
main(int argc, const char* const* argv, const char* const* env)
{
    // process command-line arguments
    svnstsw_args_t args;
    parse_args(&args, &argc, &argv, &env);

    // are we running with elevated permissions?
    if ((getuid() != geteuid()) || (getgid() != getegid()))
    {
        // Yes, so run some filesystem permissions tests.  The purpose
        // of these tests is to encourage non-malicious users to
        // report security problems to the repository administrator
        // before a malicious user comes along.

        // can Svnserve be modified or swapped with another
        // executable?
        errno = 0;
        if (svnstsw_fso_is_changeable(Svnserve))
        {
            if (errno)
                perror("Error: svnstsw svnstsw_fso_is_changeable(Svnserve)");
            else
                fprintf(stderr, "Error: svnstsw: Unsafe execution "
                        "detected.  This program is running with "
                        "elevated privileges but the user can replace "
                        "the target executable (%s).  Please check "
                        "filesystem permissions.\n", Svnserve);
            return 1;
        }

        // does the user have the ability to modify the repository?
        errno = 0;
        if (svnstsw_fso_is_changeable(args.root))
        {
            if (errno)
                perror("Error: svnstsw svnstsw_fso_is_changeable(args.root)");
            else
                fprintf(stderr, "Error: svnstsw: Unsafe repository "
                        "filesystem permissions detected.  Please "
                        "check the filesystem permissions on '%s' and "
                        "its parent directories.\n", args.root);
            return 1;
        }
    }

    // quick but incomplete check to see if we're going to run in an
    // infinite loop
    if (is_equivalent_file(argv[0], Svnserve))
    {
        fprintf(stderr, "Error: svnstsw: Infinite loop detected.  "
                "The file to execute (%s) matches this executable (%s).\n",
                Svnserve, argv[0]);
        return 1;
    }

    // create a buffer for holding the user's login name
    char tunnel_user[svnstsw_get_tunnel_user_name(NULL, 0) + 1];

    // fetch the user's login name
    {
        int tunnel_user_size = svnstsw_get_tunnel_user_name(
            tunnel_user, sizeof(tunnel_user));

        // handle any errors
        if (tunnel_user_size < 0)
        {
            perror("Error: svnstsw svnstsw_get_tunnel_user_name()");
            exit(1);
        }

        // make sure the buffer was the right size
        assert(tunnel_user_size == (sizeof(tunnel_user) - 1));
    }

    // set the file mode creation mask.  By default, configure will
    // set Svnstsw_umask such that the read, write, and execute bits
    // for other are turned off and the read, write, and execute bits
    // for the user and group are allowed.  This is to preserve
    // confidentiality of information stored in the repository and to
    // make sure that SGID installations work properly.
    umask(Svnstsw_umask);

    // argument vector to use in case the user passed in -R or
    // --read-only
    const char* const read_only_argv[] = {Svnserve, "--read-only", NULL};

    // run svnserve.  Note that we're passing an empty environment
    if (svnstsw_exec_svnserve(Svnserve, args.root, tunnel_user,
                              (args.read_only) ? read_only_argv : NULL,
                              NULL) == -1)
    {
        // exec failed -- print the error message and return
        perror("Error: svnstsw svnstsw_exec_svnserve()");
        return 1;
    }
    // should not be possible to get here
    abort();
}

/**
 * @brief Processes the command-line arguments passed to main()
 *
 * @param args_p Pointer to a structure that will hold the results of
 * processing the command-line values.
 *
 * @param argc_p Pointer to the @a argc parameter of main().  The
 * value of @a argc might be modified (via a call to APR's
 * apr_app_initialize()).
 *
 * @param argv_p Pointer to the @a argv parameter of main().  The
 * value of @a argv might be modified (via a call to APR's
 * apr_app_initialize()).
 *
 * @param env_p Pointer to the @a env parameter of main().  The value
 * of @a env might be modified (via a call to APR's
 * apr_app_initialize()).
 */
void
parse_args(svnstsw_args_t* args_p, int* argc_p, const char* const** argv_p,
           const char* const** env_p)
{
    apr_pool_t* pool = initialize_apr(argc_p, argv_p, env_p);

    // for convenience so I don't have to constantly dereference the
    // variables
    int argc = *argc_p;
    const char* const* argv = *argv_p;

    apr_status_t apr_status = 0;

    // initialize getopt
    apr_getopt_t *os;
    apr_status = apr_getopt_init(&os, pool, argc, argv);
    if (apr_status)
    {
        char buf[1024];
        apr_strerror(apr_status, buf, sizeof(buf) - 1);
        fprintf(stderr, "%s:  Error:  cannot initialize getopt:  %s\n",
                argv[0], buf);
        exit(1);
    }

    // default argument values
    args_p->read_only = 0;
    args_p->root = Svnserve_default_root;

    // parse the arguments
    while (1)
    {
        // read the next argument
        int opt;
        const char* arg;
        apr_status = apr_getopt_long(os, options, &opt, &arg);

        // are we done processing arguments?
        if (APR_STATUS_IS_EOF(apr_status))
            break;

        // was there an error with this argument?
        if (apr_status != APR_SUCCESS)
        {
            print_usage(argv[0], stderr);
            exit(1);
        }

        // process the argument
        switch (opt)
        {
            case 'h':
                // help
                print_usage(argv[0], stdout);
                exit(0);

            case SVNSERVE_OPT_VERSION:
                // version
                printf(PACKAGE_NAME " version " PACKAGE_VERSION "\n");
                printf("Copyright (c) 2008 BBN Technologies Corp.  All "
                       "rights reserved.\n\n");
                print_executable_and_path(stdout);
                exit(0);

            case 'R':
                // read-only
                args_p->read_only = 1;
                break;

            case 'r':
                // root
                if (Allow_svnserve_root_override)
                    args_p->root = arg;
                break;

            case 't':
            case SVNSERVE_OPT_TUNNEL_USER:
                // ignored
                break;

            default:
                // should not be possible to get here
                abort();
        }
    }

    // make sure all arguments were processed
    if (os->ind != argc)
    {
        print_usage(argv[0], stderr);
        exit(1);
    }
}

/**
 * @brief Prints a usage summary to @a out.
 *
 * @param argv0 the name of this executable
 *
 * @param out the output stream to write the message to (e.g., @p
 * stdout or @p stderr)
 */
void
print_usage(const char* argv0, FILE* out)
{
    fprintf(out, "usage: %s [options]\n\n", argv0);
    fprintf(out, "Valid options:\n");

    // loop through each argument and print its usage
    for (apr_size_t i = 0; options[i].name || options[i].optch; ++i)
    {
        const apr_getopt_option_t* o = options + i;
        const _Bool has_short = ((o->optch <= 255) && (o->optch > 0)) ? 1 : 0;

        assert(has_short || o->name);

        // number of columns before the description
        int noutput = 26;
        noutput -= fprintf(out, "  ");

        // print the short option
        if (has_short)
            noutput -= fprintf(out, "-%c%s",
                               o->optch, (o->has_arg) ? " ARG" : "");

        // print the long option
        if (o->name)
            noutput -= fprintf(out, "%s--%s%s",
                               (has_short) ? ", " : "",
                               o->name, (o->has_arg) ? "=ARG" : "");

        // print the description
        fprintf(out, " %*s: %s\n", (noutput > 0) ? noutput : 0, "",
                o->description);
    }

    fprintf(out, "\n");
    print_executable_and_path(out);
}

/**
 * @brief Prints the path to the @p svnserve executable and the
 * (default) root path to @a out.
 *
 * @param out the output stream to write the message to (e.g., @p
 * stdout or @p stderr)
 */
void
print_executable_and_path(FILE* out)
{
    // first, fetch the user's login name so that we can show the user
    // what we are going to pass with the --tunnel-user argument.

    // create a buffer for holding the user's login name
    char tunnel_user_buf[svnstsw_get_tunnel_user_name(NULL, 0) + 1];
    const char* tunnel_user = tunnel_user_buf;

    // fetch the user's login name
    {
        int tunnel_user_size = svnstsw_get_tunnel_user_name(
            tunnel_user_buf, sizeof(tunnel_user_buf));
        // handle any errors
        if (tunnel_user_size < 0)
            tunnel_user = "<user>";

        // make sure the buffer was the right size
        assert(tunnel_user_size == (sizeof(tunnel_user_buf) - 1));
    }

    // let the user know which executable is run and what parameters
    // are passed
    fprintf(out, "Executable to run: %s [--read-only] --root=%s --tunnel "
                 "--tunnel-user=%s\n", Svnserve,
            (Allow_svnserve_root_override) ? "<dir>"
                                           : Svnserve_default_root,
            tunnel_user);

    // if the user can specify the root, let the user know what the
    // default is
    if (Allow_svnserve_root_override)
        fprintf(out, "Default root directory to serve: %s\n",
                Svnserve_default_root);
}

/**
 * @brief Tests if two file names refer to the same file (like the @p
 * -ef operator from @p test).
 *
 * @param file1 the name of the first file to check
 *
 * @param file2 the name of the second file to check
 *
 * @return Returns 1 if the two files identified by @a file1 and @a
 * file2 share device and inode numbers.  Returns 0 if there is an
 * error or if the two file names refer to different files.  On error,
 * @p errno is set.  Error values and conditions are described in the
 * specification for stat().
 */
_Bool
is_equivalent_file(const char* file1, const char* file2)
{
    struct stat file1_stat;
    struct stat file2_stat;

    return ((stat(file1, &file1_stat) == 0)
            && (stat(file2, &file2_stat) == 0)
            && (file1_stat.st_dev == file2_stat.st_dev)
            && (file1_stat.st_ino == file2_stat.st_ino));
}

/**
 * @brief Initialize the Apache Portable Runtime (APR) and create a
 * root pool.
 *
 * @param argc_p Pointer to the @a argc parameter of main()
 *
 * @param argv_p Pointer to the @a argv parameter of main()
 *
 * @param env_p Pointer to the @a env parameter of main()
 *
 * @return Pointer to the newly-created root APR pool
 */
apr_pool_t*
initialize_apr(int* argc_p, const char* const** argv_p,
               const char* const** env_p)
{
    apr_status_t apr_status = 0;

    // initialize APR.
    apr_status = apr_app_initialize(argc_p, argv_p, env_p);
    if (apr_status)
    {
        char buf[1024];
        apr_strerror(apr_status, buf, sizeof(buf) - 1);
        fprintf(stderr, "%s:  Error:  cannot initialize APR: %s\n",
                (*argv_p)[0], buf);
        exit(1);
    }

    // make sure apr_terminate is called when we exit
    if (atexit(apr_terminate) != 0)
    {
        fprintf(stderr, "%s:  Error:  atexit(apr_terminate) failed\n",
                (*argv_p)[0]);
        exit(1);
    }

    // create an APR pool
    apr_pool_t* pool = NULL;
    apr_status = apr_pool_create_ex(&pool, NULL, abort_on_pool_failure, NULL);
    if (apr_status)
    {
        char buf[1024];
        apr_strerror(apr_status, buf, sizeof(buf) - 1);
        fprintf(stderr, "%s:  Error:  cannot create APR pool: %s\n",
                (*argv_p)[0], buf);
        exit(1);
    }

    return pool;
}

/**
 * @brief APR pool allocation failure handler which simply aborts.
 *
 * @param retcode Defined by the Apache Portable Runtime library
 * (apr_abortfunc_t).
 *
 * @return Defined by the Apache Portable Runtime library
 * (apr_abortfunc_t).
 */
int
abort_on_pool_failure(int retcode)
{
    // Don't translate this string! It requires memory allocation to
    // do so!  And we don't have any of it...
    fprintf(stderr, "Out of memory - terminating application.\n");
    abort();
    return -1; // prevent compiler warnings
}

/**
 * @}
 */
