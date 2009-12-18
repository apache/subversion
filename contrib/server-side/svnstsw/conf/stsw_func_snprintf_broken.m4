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


# Sets the shell variable snprintf_broken to "yes" if the snprintf()
# function is broken.

# increment the serial number any time this file is changed
# serial 0
AC_DEFUN([STSW_FUNC_SNPRINTF_BROKEN],
    [
        stsw_enable_snprintf_tests=yes
        AC_ARG_ENABLE(
            [snprintf-tests],
            [AS_HELP_STRING(
                [--disable-snprintf-tests],

                [Do not test whether the return value of snprintf()
                 conforms to the C99 standard.  Use only if you are
                 cross-compiling and you are sure that your snprintf()
                 conforms to the C99 standard.])],

            [
                if test "x$enableval" = "xno" ; then
                    stsw_enable_snprintf_tests=no
                fi
            ])
        AC_MSG_CHECKING([whether snprintf tests should be run])
        AC_MSG_RESULT([$stsw_enable_snprintf_tests])

        snprintf_broken=no
        if test "x$stsw_enable_snprintf_tests" = "xyes" ; then
            AC_CACHE_CHECK(
                [if snprintf is broken],
                [stsw_cv_func_snprintf_broken],
                [
                    AC_LANG_PUSH([C])
                    AC_RUN_IFELSE(
                        [AC_LANG_PROGRAM(
                            [[
                                #include <stdio.h>
                                #include <string.h>
                            ]],
                            [[
                                char buf[32];
                                const char* teststr = "0123456789ABCDEF";
                                memset(buf, 1, sizeof(buf));
                                if (snprintf(buf, 8, "%s", teststr) != 16)
                                    return 1;
                                if (buf[7] != '\0')
                                    return 1;
                                if (snprintf(buf, sizeof(buf), "%s", teststr) != 16)
                                    return 1;
                                return 0;
                            ]])],
                        [
                            # action if true
                            stsw_cv_func_snprintf_broken=no
                        ],
                        [
                            # action if false
                            stsw_cv_func_snprintf_broken=yes
                        ],
                        [
                            # action if cross-compiling
                            AC_MSG_ERROR([Unable to test if snprintf() is broken when cross-compiling.  Manually check snprintf() and use --disable-snprintf-tests if appropriate.])
                        ])
                    AC_LANG_POP([C])
                ])
            if test "x$stsw_cv_func_snprintf_broken" = "xyes" ; then
                snprintf_broken=yes
            fi
        fi
    ])
