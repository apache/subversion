# -*- Autoconf -*-

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
# Checks for doxygen executable.  Creates the following substituted
# variables:
#
# * DOXYGEN - full path to doxygen executable or empty string if
#   either not found or --without-doxygen specified
#
# * DOXYFILE_HAVE_DOT - "YES" or "NO" depending on whether the 'dot'
#   application from Graphviz was found.
#
# * DOXYFILE_DOT_PATH - double-quoted directory containing the 'dot'
#   executable or blank if doxygen should search the path.
#
# * DOXYFILE_EXCLUDE - space-separated list of double-quoted paths to
#   place in the Doxyfile's EXCLUDE option
#
# * DOXYFILE_PREDEFINED - space-separated list of NAME=VALUE pairs to
#   place in the Doxyfile's PREDEFINED option
#

# serial 0
AC_DEFUN([STSW_PROG_DOXYGEN],
  [
    
    DOXYFILE_EXCLUDE=""
    AC_SUBST([DOXYFILE_EXCLUDE])

    DOXYFILE_PREDEFINED="DOXYGEN_SHOULD_IGNORE_THIS=1"
    AC_SUBST([DOXYFILE_PREDEFINED])

    # checks for doxygen
    AC_ARG_WITH(
      [doxygen],
      [AS_HELP_STRING(
        [--with-doxygen=/path/to/doxygen],
        [Location of the doxygen executable (for building source code documentation).  Default is to search the path.])],
      [
        if test "x$withval" = "xno" ; then
          AC_MSG_CHECKING([for doxygen])
          DOXYGEN=""
          AC_MSG_RESULT([disabled])
        elif test "x$withval" = "xyes" ; then
          AC_PATH_PROG([DOXYGEN], [doxygen])
        else
          AC_MSG_CHECKING([for doxygen])
          DOXYGEN="$withval"
          AC_MSG_RESULT([$withval])
        fi
      ],
      [
        AC_PATH_PROG([DOXYGEN], [doxygen])
      ])
    AC_SUBST([DOXYGEN])

    # dot is used by doxygen to generate graphs
    DOXYFILE_HAVE_DOT=NO
    DOXYFILE_DOT_PATH=""
    AC_ARG_WITH(
        [dot],
        [AS_HELP_STRING(
            [--with-dot=/directory/containing/dot],
            [Directory containing the dot executable (part of Graphviz).  Default is to search the path.])],
        [
            if test "x$DOXYGEN" != "x" ; then
                if test "x$withval" = "xno" ; then
                    AC_MSG_CHECKING([for dot])
                    DOXYFILE_HAVE_DOT=NO
                    AC_MSG_RESULT([disabled])
                elif test "x$withval" = "xyes" ; then
                    AC_CHECK_PROG([stsw_have_dot], [dot], [yes], [no])
                else
                    AC_MSG_CHECKING([for dot])
                    DOXYFILE_HAVE_DOT=YES
                    DOXYFILE_DOT_PATH=\"$withval\"
                    AC_MSG_RESULT([$withval/dot])
                fi
            fi
        ],
        [
            if test "x$DOXYGEN" != "x" ; then
                AC_CHECK_PROG([stsw_have_dot], [dot], [yes], [no])
            fi
        ])
    if test "x$stsw_have_dot" = "xyes" ; then
        DOXYFILE_HAVE_DOT=YES
    elif test "x$stsw_have_dot" = "xno" ; then
        DOXYFILE_HAVE_DOT=NO
        #AC_MSG_WARN([dot (from Graphviz) not found on your system.  Doxygen documentation will not include pretty graphs.  You may specify the directory containing dot using --with-dot])
    fi
    AC_SUBST([DOXYFILE_HAVE_DOT])
    AC_SUBST([DOXYFILE_DOT_PATH])
  ])
