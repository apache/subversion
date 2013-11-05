# Copyright (c) 2008 BBN Technologies Corp.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of BBN Technologies nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY BBN TECHNOLOGIES AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL BBN TECHNOLOGIES OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

AC_INIT([svnstsw], [1.4])
AC_PREREQ([2.61])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([conf])
AC_CONFIG_MACRO_DIR([conf])
AC_CONFIG_FILES([
    Makefile
    doc/Doxyfile-html
    doc/Makefile
    doc/man/libsvnstsw.3
    doc/man/svnstsw.8
    doc/man/svnstsw_exec_svnserve.3
    doc/man/svnstsw_get_tunnel_user_name.3
    doc/man/svnstsw_fso_is_changeable.3
    include/Makefile
    src/Makefile
  ])
AC_CONFIG_HEADERS([src/config.h])
AH_BOTTOM([#include <svnstsw_compat.h>])

AM_INIT_AUTOMAKE([foreign 1.10])
AM_MAINTAINER_MODE

# Test for a C99 compiler.  Features in use:
#   * _Bool
#   * declare variables after start
#   * variable-length arrays
#   * C++-style comments
AC_PROG_CC_C99
#   * snprintf
STSW_FUNC_SNPRINTF_BROKEN
if test "x$ac_cv_prog_cc_c99" = "xno" || test "x$snprintf_broken" = "xyes" ; then
    AC_MSG_ERROR([Your C compiler does not support some of the required C99 features.])
fi

AC_PROG_LIBTOOL
AC_PROG_INSTALL

# Test for the reentrant POSIX functions we use
AC_CHECK_FUNCS([getpwuid_r getpwnam_r], [],
    [AC_MSG_ERROR([A required function was not found on your system.])])
AC_CHECK_FUNCS([getlogin_r], [], [AC_MSG_WARN([

    If multiple users share the same user ID on this system, svnserve
    may use the wrong username as the argument to svnserve's
    '--tunnel-user' parameter.  This is because the POSIX function
    getlogin_r() was not found on your system.  The function
    getpwuid_r() will be used instead, which means that another
    username corresponding to the shared UID may be returned by
    svnstsw_get_tunnel_user_name().
])])

# Have autoconf tests modify the following variables rather than
# CPPFLAGS, CFLAGS, etc.  This allows automake .am files to override
# these flags (by appending to them) without overriding user-specified
# flags.
AC_SUBST([CONFIGURECPPFLAGS])
AC_SUBST([CONFIGURECFLAGS])
AC_SUBST([CONFIGURECXXFLAGS])
AC_SUBST([CONFIGURELDFLAGS])
AC_SUBST([CONFIGURELIBTOOLFLAGS])

# increase compiler strictness
if test "x$GCC" = "xyes" ; then
    CONFIGURECFLAGS="-Wall -Werror $CONFIGURECFLAGS"
fi
if test "x$GXX" = "xyes" ; then
    CONFIGURECXXFLAGS="-Wall -Werror $CONFIGURECXXFLAGS"
fi

# The following makes sure that the include subdirectory is included,
# even in VPATH builds.  Unfortunately, autoconf doesn't define a
# shell variable containing the absolute path to the top source
# directory (at least it's not documented), so we can't use that here.
# However, it does have a preset *ouput* variable.  The following
# causes a literal '-I${top_srcdir}/include' to appear in the
# definition of the CONFIGURECPPFLAGS variable.  The '${top_srcdir}'
# is then substituted by make.  Unfortunately, this is somewhat
# broken:  If we want to do further autoconf tests using this include,
# the '${top_srcdir}' either won't be substituted or it will be
# substituted with an empty string since there is no 'top_srcdir'
# shell variable defined by autoconf.  TODO:  figure out a way around
# this.
#
CONFIGURECPPFLAGS="-I\${top_srcdir}/include $CONFIGURECPPFLAGS"

# find libapr
STSW_LIB_APR([0\.9\.@<:@7-9@:>@ 0\.9\.1@<:@0-9@:>@ 1\.])
CONFIGURECPPFLAGS="$STSW_APR_CPPFLAGS $STSW_APR_INCLUDES $CONFIGURECPPFLAGS"
CONFIGURECFLAGS="$STSW_APR_CFLAGS $CONFIGURECFLAGS"
CONFIGURELDFLAGS="$STSW_APR_LDFLAGS $STSW_APR_LIBS $CONFIGURELDFLAGS"

# find doxygen for generating the source code documentation
STSW_PROG_DOXYGEN
DOXYFILE_PREDEFINED="$DOXYFILE_PREDEFINED HAVE_CONFIG_H=1"

# Test for the location of svnserve
AC_ARG_WITH(
    [svnserve],
    [AS_HELP_STRING(
        [--with-svnserve=/path/to/svnserve],

        [Full path to the svnserve executable.  This string will be
         used literally to start svnserve; e.g.,
         execl("/given/path/to/svnserve", ...).  For security reasons,
         this can not be a relative path -- it must start with "/".
         Note that this path can point to any executable that accepts
         svnserve's arguments, such as another svnserve wrapper.  Do
         not provide a path to a script unless you are fully aware of
         the security problems that arise when a script is run as a
         different user.  If left unspecified, configure will search
         the PATH for svnserve.])],

    [
        if test "x$withval" = "xyes" ; then
            AC_PATH_PROG([SVNSERVE], [svnserve])
        else
            AC_MSG_CHECKING([for svnserve])
            SVNSERVE=$withval
            AC_MSG_RESULT([$SVNSERVE])
        fi
    ],
    [
        AC_PATH_PROG([SVNSERVE], [svnserve])
    ])
case "$SVNSERVE" in
    /*)
        # absolute path
        ;;
    *)
        # relative path
        AC_MSG_ERROR([You must specify a full path to the svnserve executable using "--with-svnserve=/path/to/svnserve"])
        ;;
esac
AC_DEFINE_UNQUOTED([SVNSERVE], ["$SVNSERVE"], [Define to the desired svnserve binary to run.])

# Ask user if root override is acceptable
stsw_root_override=yes
AC_ARG_ENABLE(
    [svnserve-root-override],
    [AS_HELP_STRING(
        [--disable-svnserve-root-override],

        [Tell svnstsw to ignore the -r and --root command-line
         options.  This prevents a user from specifying a virtual root
         path, ensuring that the default path (given by
         --with-svnserve-root-default) is always passed to the
         svnserve executable (given by --with-svnserve) using
         svnserve's --root command-line option.  Allowing a user to
         specify a different virtual root could be a security concern
         if there is a repository that is accessible to the svnstsw
         wrapper and that repository uses a username namespace that is
         different from the system username namespace.])],

    [
        if test "x$enableval" != "xyes" ; then
            stsw_root_override=no
        fi
    ])
AC_MSG_CHECKING([if users should be allowed to override the default repository virtual root])
stsw_root_override_bool=1
if test "x$stsw_root_override" = "xno" ; then
   stsw_root_override_bool=0
fi
AC_DEFINE_UNQUOTED([ALLOW_SVNSERVE_ROOT_OVERRIDE], [$stsw_root_override_bool], [Define to 1 if svnstsw should honor the -r and --root command-line arguments, 0 otherwise.])
AC_MSG_RESULT([$stsw_root_override])

# Ask the user for the default svn repository root
stsw_root_default=/
AC_ARG_WITH(
    [svnserve-root-default],
    [AS_HELP_STRING(
        [--with-svnserve-root-default=/path/to/svn/virtual/root],

        [Specify the default path to pass as the argument to
         svnserve's --root command-line option.  The given path will
         be passed to svnserve if the user of svnstsw does not pass -r
         or --root to svnstsw or if svnstsw is configured to prevent
         users from specifying a virtual root path (via
         --disable-svnserve-root-override).  If unspecified, "/" is
         used.])],

    [
        if test "x$withval" != "xyes" && test "x$withval" != "xno" ; then
            stsw_root_default=$withval
        fi
    ])
AC_MSG_CHECKING([for the default repository virtual root path])
AC_DEFINE_UNQUOTED([SVNSERVE_DEFAULT_ROOT], ["$stsw_root_default"], [Define to the desired default repository virtual root path.])
AC_MSG_RESULT([$stsw_root_default])

# Ask the user for the file mode creation mask
stsw_umask="S_IRWXO"
AC_ARG_WITH(
    [umask],
    [AS_HELP_STRING(
        [--with-umask=<mode_t expression>],

        [Specify the file mode creation mask to use when the program
         is run.  This must be a C constant expression with a type
         that is compatible with mode_t (see the umask(3) man page and
         <sys/stat.h>).  If unspecified, "S_IRWXO" is used (which
         disables read, write, and execute for others/world and allows
         read, write, and execute for the user and group).])],

    [
        if test "x$withval" != "xyes" && test "x$withval" != "xno" ; then
            stsw_umask=$withval
        fi
    ])
AC_MSG_CHECKING([for the file mode creation mask])
AC_DEFINE_UNQUOTED([SVNSTSW_UMASK()], [($stsw_umask)], [Define to the desired file mode creation mask.])
AC_MSG_RESULT([$stsw_umask])

AC_OUTPUT
