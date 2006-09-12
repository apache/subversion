dnl modules enabled in this directory by default

APACHE_MODPATH_INIT(dav/svn)

dnl dav_svn_objects=""

dnl ### we want to default this based on whether dav is being used...
dnl ### but there is no ordering to the config.m4 files right now...
dnl APACHE_MODULE(dav_svn, DAV provider for Subversion, $dav_svn_objects, , no)

AC_MSG_CHECKING(whether to enable mod_dav_svn)
AC_ARG_ENABLE(dav-svn,
  AC_HELP_STRING([--enable-dav-svn], [DAV provider for Subversion]),
  [  ],
  [ enable_dav_svn=no ])
AC_MSG_RESULT($enable_dav_svn)

if test "$enable_dav_svn" != "no"; then
  case "$enable_$1" in
    shared*)
      AC_MSG_ERROR(mod_dav_svn can only be built dynamically via APXS)
      ;;
    *)
      MODLIST="$MODLIST dav_svn"
      ;;
  esac

  modpath_static="libmod_dav_svn.la libsvn_fs.la libsvn_subr.la"
  BUILTIN_LIBS="$BUILTIN_LIBS $modpath_current/libmod_dav_svn.la $modpath_current/libsvn_fs.la $modpath_current/libsvn_subr.la"
fi

APACHE_MODPATH_FINISH
