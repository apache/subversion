dnl
dnl  SVN_LIB_KWALLET
dnl
dnl  Check configure options and assign variables related to KWallet support
dnl

AC_DEFUN(SVN_LIB_KWALLET,
[
  AC_ARG_WITH(kwallet,
    [AS_HELP_STRING([[--with-kwallet[=PATH]]],
                    [Enable use of KWallet (KDE 4) for auth credentials])],
                    [svn_lib_kwallet="$withval"],
                    [svn_lib_kwallet=no])

  AC_MSG_CHECKING([whether to look for KWallet])
  if test "$svn_lib_kwallet" != "no"; then
    AC_MSG_RESULT([yes])
    if test "$enable_shared" = "yes"; then
      if test "$APR_HAS_DSO" = "yes"; then
        if test "$USE_NLS" = "yes"; then
          if test -n "$PKG_CONFIG"; then
            if test "$HAVE_DBUS" = "yes"; then
              AC_MSG_CHECKING([for QtCore, QtDBus, QtGui])
              if $PKG_CONFIG --exists QtCore QtDBus QtGui; then
                AC_MSG_RESULT([yes])
                if test "$svn_lib_kwallet" != "yes"; then
                  AC_MSG_CHECKING([for kde4-config])
                  kde4_config="$svn_lib_kwallet/bin/kde4-config"
                  if test -f "$kde4_config" && test -x "$kde4_config"; then
                    HAVE_KDE4_CONFIG="yes"
                    AC_MSG_RESULT([yes])
                  else
                    AC_MSG_RESULT([no])
                  fi
                else
                  AC_CHECK_PROG(HAVE_KDE4_CONFIG, kde4-config, yes)
                  kde4_config="kde4-config"
                fi
                if test "$HAVE_KDE4_CONFIG" = "yes"; then
                  AC_MSG_CHECKING([for KWallet])
                  old_CXXFLAGS="$CXXFLAGS"
                  old_LDFLAGS="$LDFLAGS"
                  old_LIBS="$LIBS"
                  for d in [`$PKG_CONFIG --cflags QtCore QtDBus QtGui`]; do
                    if test -n ["`echo "$d" | $GREP -- '^-D[^[:space:]]*'`"]; then
                      CPPFLAGS="$CPPFLAGS $d"
                    fi
                  done
                  qt_include_dirs="`$PKG_CONFIG --cflags-only-I QtCore QtDBus QtGui`"
                  kde_dir="`$kde4_config --prefix`"
                  SVN_KWALLET_INCLUDES="$DBUS_CPPFLAGS $qt_include_dirs -I$kde_dir/include"
                  qt_libs_other_options="`$PKG_CONFIG --libs-only-other QtCore QtDBus QtGui`"
                  SVN_KWALLET_LIBS="$DBUS_LIBS -lQtCore -lQtDBus -lQtGui -lkdecore -lkdeui $qt_libs_other_options"
                  CXXFLAGS="$CXXFLAGS $SVN_KWALLET_INCLUDES"
                  LIBS="$LIBS $SVN_KWALLET_LIBS"
                  qt_lib_dirs="`$PKG_CONFIG --libs-only-L QtCore QtDBus QtGui`"
                  LDFLAGS="$old_LDFLAGS $qt_lib_dirs -L$kde_dir/lib`$kde4_config --libsuffix`"
                  AC_LANG(C++)
                  AC_LINK_IFELSE([
#include <kwallet.h>
int main()
{KWallet::Wallet::walletList();}], svn_lib_kwallet="yes", svn_lib_kwallet="no")
                  AC_LANG(C)
                  if test "$svn_lib_kwallet" = "yes"; then
                    AC_MSG_RESULT([yes])
                    CXXFLAGS="$old_CXXFLAGS"
                    LIBS="$old_LIBS"
                  else
                    AC_MSG_RESULT([no])
                    AC_MSG_ERROR([cannot find KWallet])
                  fi
                else
                  AC_MSG_ERROR([cannot find kde4-config])
                fi
              else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([cannot find QtCore, QtDBus, QtGui])
              fi
            else
              AC_MSG_ERROR([cannot find D-Bus])
            fi
          else
            AC_MSG_ERROR([cannot find pkg-config])
          fi
        else
          AC_MSG_ERROR([missing support for internationalization])
        fi
      else
        AC_MSG_ERROR([APR does not have support for DSOs])
      fi
    else
      AC_MSG_ERROR([--with-kwallet conflicts with --disable-shared])
    fi
  else
    AC_MSG_RESULT([no])
  fi
  AC_SUBST(SVN_KWALLET_INCLUDES)
  AC_SUBST(SVN_KWALLET_LIBS)
])
