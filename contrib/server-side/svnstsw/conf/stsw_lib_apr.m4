# -*- Autoconf -*-

# TODO:  Figure out licensing of this file (it is derived with heavy
# modification from apr.m4 from Subversion)

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

#
# NAME
#        STSW_LIB_APR - Check for the Apache Portable Runtime library
#
# SYNOPSIS
#        STSW_LIB_APR(wanted_regex)
#
# DESCRIPTION
#        Searches for the Apache Portable Runtime (APR) headers and
#        library and determines the appropriate build flags.
#
# ARGUMENTS
#        wanted_regex
#               Regular expression that the APR version string must
#               match.
#
# RETURN VALUE
#        The shell variable 'apr_found' is set to 'yes' if APR is
#        found.
#
#        If APR is found, the following substituted variables are set
#        according to the output of apr-config:  STSW_APR_CFLAGS,
#        STSW_APR_CPPFLAGS, STSW_APR_INCLUDES, STSW_APR_LDFLAGS, and
#        STSW_APR_LIBS
#
# NOTES
#        Derived from Subversion's apr.m4.
#

# serial 0
AC_DEFUN([STSW_LIB_APR],
  [
    AC_MSG_NOTICE([Apache Portable Runtime (APR) library configuration])

    APR_FIND_APR([], [], [1], [0 1])
    if test "x$apr_found" != "xyes" ; then
        AC_MSG_ERROR([the Apache Portable Runtime (APR) library (version 0.x or 1.x) was not found.  Please specify a path to APR using '--with-apr'.])
    fi

    # check APR version number against regex  

    APR_WANTED_REGEXES="$1"
    if test "x$APR_WANTED_REGEXES" = "x" ; then
        AC_MSG_ERROR([internal error:  invalid argument to [STSW_LIB_APR]])
    fi

    AC_MSG_CHECKING([APR version])
    apr_version=`$apr_config --version`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --version' failed])
    fi
    AC_MSG_RESULT([$apr_version])

    apr_version_regex_match=no
    for apr_wanted_regex in $APR_WANTED_REGEXES; do
        AC_MSG_CHECKING([if APR version matches '$apr_wanted_regex'])
        if test `expr $apr_version : $apr_wanted_regex` -ne 0; then
            apr_version_regex_match=yes
        fi
        AC_MSG_RESULT([$apr_version_regex_match])
        if test "x$apr_version_regex_match" = "xyes" ; then
            break
        fi
    done

    if test "x$apr_version_regex_match" != "xyes" ; then
        AC_MSG_ERROR([APR version mismatch])
    fi

    # Get build information from APR

    AC_MSG_CHECKING([for APR CPPFLAGS])
    STSW_APR_CPPFLAGS=`$apr_config --cppflags`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --cppflags' failed])
    fi
    AC_MSG_RESULT([$STSW_APR_CPPFLAGS])

    AC_MSG_CHECKING([for APR CFLAGS])
    STSW_APR_CFLAGS=`$apr_config --cflags`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --cflags' failed])
    fi
    AC_MSG_RESULT([$STSW_APR_CFLAGS])

    AC_MSG_CHECKING([for APR LDFLAGS])
    STSW_APR_LDFLAGS=`$apr_config --ldflags`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --ldflags' failed])
    fi
    AC_MSG_RESULT([$STSW_APR_LDFLAGS])

    AC_MSG_CHECKING([for APR includes])
    STSW_APR_INCLUDES=`$apr_config --includes`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --includes' failed])
    fi
    AC_MSG_RESULT([$STSW_APR_INCLUDES])

    AC_MSG_CHECKING([for APR LIBS])
    STSW_APR_LIBS=`$apr_config --link-ld`
    if test $? -ne 0; then
        AC_MSG_RESULT([failed])
        AC_MSG_ERROR(['apr-config --link-ld' failed])
    fi
    AC_MSG_RESULT([$STSW_APR_LIBS])

    # substitute the variables so that Makefiles can selectively
    # enable the flags

    AC_SUBST([STSW_APR_CPPFLAGS])
    AC_SUBST([STSW_APR_CFLAGS])
    AC_SUBST([STSW_APR_LDFLAGS])
    AC_SUBST([STSW_APR_INCLUDES])
    AC_SUBST([STSW_APR_LIBS])

  ])
