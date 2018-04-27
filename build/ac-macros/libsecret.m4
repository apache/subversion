dnl ===================================================================
dnl   Licensed to the Apache Software Foundation (ASF) under one
dnl   or more contributor license agreements.  See the NOTICE file
dnl   distributed with this work for additional information
dnl   regarding copyright ownership.  The ASF licenses this file
dnl   to you under the Apache License, Version 2.0 (the
dnl   "License"); you may not use this file except in compliance
dnl   with the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl   Unless required by applicable law or agreed to in writing,
dnl   software distributed under the License is distributed on an
dnl   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl   KIND, either express or implied.  See the License for the
dnl   specific language governing permissions and limitations
dnl   under the License.
dnl ===================================================================
dnl
dnl  SVN_LIB_SECRET
dnl
dnl  Check configure options and assign variables related to libsecret support

AC_DEFUN(SVN_LIB_SECRET,
[
  AC_ARG_WITH(gnome_keyring,
    AS_HELP_STRING([--with-gnome-keyring],
                   [Enable GNOME Keyring for auth credentials (enabled by default if found)]),
                   [with_gnome_keyring="$withval"],
                   [with_gnome_keyring=auto])

  found_gnome_keyring="no"
  AC_MSG_CHECKING([whether to look for GNOME Keyring])
  if test "$found_old_gnome_keyring" = "yes" && test "$with_gnome_keyring" = "auto"; then
    with_gnome_keyring="no"
  fi
  if test "$with_gnome_keyring" != "no"; then
    AC_MSG_RESULT([yes])
    case "$host" in
    *-*-darwin*)
      if test "$with_gnome_keyring" = "yes"; then
        AC_MSG_ERROR([--with-gnome-keyring is not supported on Mac OS X.])
      fi
      ;;
    *)
      AC_MSG_CHECKING([for GNOME Keyring])
      if test "$found_old_gnome_keyring" = "no"; then
        if test "$svn_enable_shared" = "yes"; then
          if test "$APR_HAS_DSO" = "yes"; then
            if test -n "$PKG_CONFIG"; then
              if $PKG_CONFIG --exists libsecret-1; then
                AC_MSG_RESULT([yes])
                AC_DEFINE([SVN_HAVE_LIBSECRET], [1],
                          [Is libsecret support enabled?])
                SVN_GNOME_KEYRING_INCLUDES="`$PKG_CONFIG --cflags libsecret-1`"
                SVN_GNOME_KEYRING_LIBS="`$PKG_CONFIG --libs libsecret-1`"
                found_gnome_keyring="yes"
              else
                if test "$with_gnome_keyring" = "yes"; then
                  AC_MSG_ERROR([cannot find libsecret])
                fi
              fi
            else
              if test "$with_gnome_keyring" = "yes"; then
                AC_MSG_ERROR([cannot find pkg-config])
              fi
            fi
          else
            if test "$with_gnome_keyring" = "yes"; then
              AC_MSG_ERROR([APR does not support DSOs])
            fi
          fi
        else
          if test "$with_gnome_keyring" = "yes"; then
            AC_MSG_ERROR([--with-gnome-keyring conflicts with --disable-shared])
          fi
        fi
      else
        if test "$with_gnome_keyring" = "yes"; then
          AC_MSG_ERROR([--with-gnome-keyring conflicts with --with-old-gnome-keyring])
        fi
      fi
      if test "$found_gnome_keyring" = "no"; then
        AC_MSG_RESULT([no])
      fi
      ;;
    esac
  else
    AC_MSG_RESULT([no])
  fi
  AC_SUBST(SVN_GNOME_KEYRING_INCLUDES)
  AC_SUBST(SVN_GNOME_KEYRING_LIBS)
])
