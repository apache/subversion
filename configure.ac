dnl configure.ac: Autoconfiscation for Subversion
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)

dnl Get the version of Subversion, using m4's esyscmd() command to do this
dnl at m4-time, since AC_INIT() requires it then.
AC_INIT([subversion],
     [esyscmd(python build/getversion.py SVN subversion/include/svn_version.h)],
     [http://subversion.tigris.org/])

AC_CONFIG_SRCDIR(subversion/include/svn_types.h)
AC_CONFIG_AUX_DIR([build])

AC_MSG_NOTICE([Configuring Subversion ]AC_PACKAGE_VERSION)

AC_SUBST([abs_srcdir], ["`cd $srcdir && pwd`"])
AC_SUBST([abs_builddir], ["`pwd`"])
if test "$abs_srcdir" = "$abs_builddir"; then
  canonicalized_srcdir=""
else
  canonicalized_srcdir="$srcdir/"
fi
AC_SUBST([canonicalized_srcdir])

SWIG_LDFLAGS="$LDFLAGS"
AC_SUBST([SWIG_LDFLAGS])

# Generate config.nice early (before the arguments are munged)
SVN_CONFIG_NICE(config.nice)

# ==== Check for programs ====================================================

# Look for a C compiler (before anything can set CFLAGS)
AC_PROG_CC

# Look for a C++ compiler
AC_PROG_CXX

# Look for a C pre-processor
AC_PROG_CPP

# Grab target_cpu, so we can use it in the Solaris pkginfo file
AC_CANONICAL_TARGET

# Look for an extended grep
AC_PROG_EGREP

AC_PROG_LN_S

AC_PROG_INSTALL
# If $INSTALL is relative path to our fallback install-sh, then convert
# to an absolute path, as in some cases (e.g. Solaris VPATH build), libtool
# may try to use it from a changed working directory.
if test "$INSTALL" = "build/install-sh -c"; then
  INSTALL="$abs_srcdir/$INSTALL"
fi

AC_SUBST([MKDIR], ["$INSTALL -d"])

# ==== Libraries, for which we may have source to build ======================

dnl verify apr version and set apr flags
dnl These regular expressions should not contain "\(" and "\)".
dnl The specific reason we require APR 0.9.7 is:
dnl   It contains fixes to its file writing routines
dnl   now generating errors instead of silently ignoring
dnl   them.  Only .7 and later can guarantee repository
dnl   integrity with FSFS.

APR_VER_REGEXES=["0\.9\.[7-9] 0\.9\.1[0-9] 1\."]
APU_VER_REGEXES=["0\.9\.[7-9] 0\.9\.1[0-9] 1\."]

SVN_LIB_APR($APR_VER_REGEXES)
SVN_LIB_APRUTIL($APU_VER_REGEXES)

dnl Search for pkg-config
AC_PATH_PROG(PKG_CONFIG, pkg-config)

# Either a space-separated list of allowable Neon versions, or "any" to
# mean allow anything.
NEON_ALLOWED_LIST="0\.25 0\.26 0\.27\.2 0\.28"
NEON_RECOMMENDED_VER=0.28.4
NEON_URL="http://www.webdav.org/neon/neon-${NEON_RECOMMENDED_VER}.tar.gz"
dnl You can skip the neon version check only if you know what you are doing 
AC_ARG_ENABLE(neon-version-check,
  AS_HELP_STRING([--disable-neon-version-check],
                 [do not check the Neon version]),
  [
    if test "$enableval" = "no" ; then
      NEON_ALLOWED_LIST="any"
    fi
  ],
  [])
SVN_LIB_NEON($NEON_ALLOWED_LIST, $NEON_RECOMMENDED_VER, $NEON_URL)

if test "$svn_lib_neon" = "yes"; then
  AC_DEFINE([SVN_HAVE_NEON], 1,
            [Defined if support for Neon is enabled])
fi

dnl Search for serf as an alternative to neon
SVN_LIB_SERF

if test "$svn_lib_serf" = "yes"; then
  AC_DEFINE([SVN_HAVE_SERF], 1,
            [Defined if support for Serf is enabled])
fi

dnl Search for apr_memcache (only affects fs_fs)
SVN_LIB_APR_MEMCACHE

if test "$svn_lib_apr_memcache" = "yes"; then
  AC_DEFINE(SVN_HAVE_MEMCACHE, 1,
            [Defined if apr_memcache (standalone or in apr-util) is present])
fi


dnl Find Apache with a recent-enough magic module number
SVN_FIND_APACHE(20020903)

dnl Search for SQLite
SQLITE_MINIMUM_VER="3.4.0"
SQLITE_RECOMMENDED_VER="3.6.13"
SQLITE_URL="http://www.sqlite.org/sqlite-amalgamation-${SQLITE_RECOMMENDED_VER}.tar.gz"
SVN_LIB_SQLITE(${SQLITE_MINIMUM_VER}, ${SQLITE_RECOMMENDED_VER},
               ${SQLITE_URL})

dnl Set up a number of directories ---------------------

dnl Create SVN_BINDIR for proper substitution
if test "${bindir}" = '${exec_prefix}/bin'; then
        if test "${exec_prefix}" = "NONE"; then
                if test "${prefix}" = "NONE"; then
                        SVN_BINDIR="${ac_default_prefix}/bin"
                else
                        SVN_BINDIR="${prefix}/bin"
                fi
        else
                SVN_BINDIR="${exec_prefix}/bin"
        fi
else
        SVN_BINDIR="${bindir}"
fi

dnl fully evaluate this value. when we substitute it into our tool scripts,
dnl they will not have things such as ${bindir} available
SVN_BINDIR="`eval echo ${SVN_BINDIR}`"
AC_SUBST(SVN_BINDIR)

dnl provide ${bindir} in svn_private_config.h for use in compiled code
AC_DEFINE_UNQUOTED(SVN_BINDIR, "${SVN_BINDIR}",
        [Defined to be the path to the installed binaries])

dnl This purposely does *not* allow for multiple parallel installs.
dnl However, it is compatible with most gettext usages.
localedir='${datadir}/locale'
AC_SUBST(localedir)

dnl For SVN_LOCALE_DIR, we have to expand it to something.  See SVN_BINDIR.
if test "${datadir}" = '${prefix}/share' && test "${prefix}" = "NONE"; then
  exp_localedir='${ac_default_prefix}/share/locale'
else
  exp_localedir=$localedir
fi
SVN_EXPAND_VAR(svn_localedir, "${exp_localedir}")
AC_DEFINE_UNQUOTED(SVN_LOCALE_DIR, "${svn_localedir}",
                   [Defined to be the path to the installed locale dirs])

dnl Check for libtool -- we'll definitely need it for all our shared libs!
echo "configuring libtool now"
AC_ARG_ENABLE(experimental-libtool,
  AS_HELP_STRING([--enable-experimental-libtool],[Use APR's libtool]),
  [experimental_libtool=$enableval],[experimental_libtool=no])

if test "$experimental_libtool" = "yes"; then
  echo "using APR's libtool"
  sh_libtool="`$apr_config --apr-libtool`"
  LIBTOOL="$sh_libtool"
  SVN_LIBTOOL="$sh_libtool"
else
  sh_libtool="$abs_builddir/libtool"
  SVN_LIBTOOL="\$(SHELL) $sh_libtool"
  dnl libtoolize requires that the following line not be indented
ifdef([LT_INIT], [LT_INIT], [AC_PROG_LIBTOOL])
fi
AC_SUBST(SVN_LIBTOOL)

dnl Before configuring libtool check for --enable-all-static option
AC_ARG_ENABLE(all-static,
  AS_HELP_STRING([--enable-all-static],
                 [Build completely static (standalone) binaries.]),
  [
    if test "$enableval" = "yes" ; then
      LT_LDFLAGS="-all-static $LT_LDFLAGS"
    fi
])

AC_SUBST(LT_LDFLAGS)

dnl Check if -no-undefined is needed for the platform.
dnl It should always work but with libtool 1.4.3 on OS X it breaks the build.
dnl So we only turn it on for platforms where we know we really need it.
AC_MSG_CHECKING([whether libtool needs -no-undefined])
case $host in
  *-*-cygwin*)
    AC_MSG_RESULT([yes])
    LT_NO_UNDEFINED="-no-undefined"
    ;;
  *)
    AC_MSG_RESULT([no])
    LT_NO_UNDEFINED=""
    ;;
esac
AC_SUBST(LT_NO_UNDEFINED)

AC_MSG_CHECKING([whether to avoid circular linkage at all costs])
case $host in
  *-*-cygwin*)
    AC_MSG_RESULT([yes])
    AC_DEFINE([SVN_AVOID_CIRCULAR_LINKAGE_AT_ALL_COSTS_HACK], 1,
              [Define if circular linkage is not possible on this platform.])
    ;;
  *)
    AC_MSG_RESULT([no])
    ;;
esac

dnl Check for trang.
trang=yes
AC_ARG_WITH(trang,
AS_HELP_STRING([--with-trang=PATH],
               [Specify the command to run the trang schema converter]),
[
    trang="$withval"
])
if test "$trang" = "yes"; then
    AC_PATH_PROG(TRANG, trang, none)
else
    TRANG="$trang"
    AC_SUBST(TRANG)
fi


dnl Check for libraries --------------------

# Berkeley DB on SCO OpenServer needs -lsocket
AC_CHECK_LIB(socket, socket)

# Build the BDB filesystem library only if we have an appropriate
# version of Berkeley DB.
case "$host" in
powerpc-apple-darwin*)
    # Berkeley DB 4.0 does not work on OS X.
    SVN_FS_WANT_DB_MAJOR=4
    SVN_FS_WANT_DB_MINOR=1
    SVN_FS_WANT_DB_PATCH=25
    ;;
*)
    SVN_FS_WANT_DB_MAJOR=4
    SVN_FS_WANT_DB_MINOR=0
    SVN_FS_WANT_DB_PATCH=14
    ;;
esac
# Look for libdb4.so first:
SVN_LIB_BERKELEY_DB($SVN_FS_WANT_DB_MAJOR, $SVN_FS_WANT_DB_MINOR,
                    $SVN_FS_WANT_DB_PATCH, [db4 db])

AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_MAJOR, $SVN_FS_WANT_DB_MAJOR,
                   [The desired major version for the Berkeley DB])
AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_MINOR, $SVN_FS_WANT_DB_MINOR,
                   [The desired minor version for the Berkeley DB])
AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_PATCH, $SVN_FS_WANT_DB_PATCH,
                   [The desired patch version for the Berkeley DB])

AC_SUBST(SVN_DB_INCLUDES)
AC_SUBST(SVN_DB_LIBS)

SVN_LIB_SASL

if test "$svn_lib_sasl" = "yes"; then
  AC_DEFINE(SVN_HAVE_SASL, 1,
            [Defined if Cyrus SASL v2 is present on the system])
fi

dnl Mac OS KeyChain -------------------

AC_ARG_ENABLE(keychain,
  AS_HELP_STRING([--disable-keychain], 
  [Disable use of Mac OS KeyChain for auth credentials]),
  [enable_keychain=$enableval],[enable_keychain=yes])

AC_MSG_CHECKING([for Mac OS KeyChain Services])

if test "$enable_keychain" = "yes"; then
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <AvailabilityMacros.h>
#if !DARWIN || (MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_2)
#error KeyChain API unavailable.
#endif]], [[]])],[],[enable_keychain=no])
fi

if test "$enable_keychain" = "yes"; then
  dnl ### Hack.  We should only need to pass the -framework options when
  dnl linking libsvn_subr, since it is the only library that uses Keychain.
  dnl
  dnl Unfortunately, libtool 1.5.x doesn't track transitive dependencies for
  dnl OS X frameworks like it does for normal libraries, so we need to
  dnl explicitly pass the option to all the users of libsvn_subr to allow
  dnl static builds to link successfully.
  dnl
  dnl This does mean that all executables we link will be linked directly
  dnl to these frameworks - even when building shared libraries - but that
  dnl shouldn't cause any problems.
  LIBS="$LIBS -framework Security"
  LIBS="$LIBS -framework CoreFoundation"
  LIBS="$LIBS -framework CoreServices"
  AC_DEFINE([SVN_HAVE_KEYCHAIN_SERVICES], [1], [Is Mac OS KeyChain support enabled?])
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi


dnl APR_HAS_DSO -------------------

AC_MSG_CHECKING([whether APR has support for DSOs])
old_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES"
AC_PREPROC_IFELSE([
#include <apr.h>
#if !APR_HAS_DSO
#error
#endif],
  APR_HAS_DSO="yes"
  AC_MSG_RESULT([yes]),
  APR_HAS_DSO="no"
  AC_MSG_RESULT([no]))
CPPFLAGS="$old_CPPFLAGS"


dnl D-Bus (required for support for KWallet and GNOME Keyring) -------------------

if test -n "$PKG_CONFIG"; then
  AC_MSG_CHECKING([for D-Bus .pc file])
  if $PKG_CONFIG --exists dbus-1; then
    AC_MSG_RESULT([yes])
    old_CPPFLAGS="$CPPFLAGS"
    old_LIBS="$LIBS"
    DBUS_CPPFLAGS="`$PKG_CONFIG --cflags dbus-1`"
    AC_MSG_CHECKING([D-Bus version])
    DBUS_VERSION="`$PKG_CONFIG --modversion dbus-1`"
    AC_MSG_RESULT([$DBUS_VERSION])
    # D-Bus 0.* requires DBUS_API_SUBJECT_TO_CHANGE
    if test -n ["`echo "$DBUS_VERSION" | $GREP '^0\.[[:digit:]]\+'`"]; then
      DBUS_CPPFLAGS="$DBUS_CPPFLAGS -DDBUS_API_SUBJECT_TO_CHANGE"
    fi
    DBUS_LIBS="`$PKG_CONFIG --libs dbus-1`"
    CPPFLAGS="$CPPFLAGS $DBUS_CPPFLAGS"
    LIBS="$LIBS $DBUS_LIBS"
    AC_MSG_CHECKING([for D-Bus])
    AC_LINK_IFELSE([
#include <dbus/dbus.h>
int main()
{dbus_bus_get(DBUS_BUS_SESSION, NULL);}], HAVE_DBUS="yes", HAVE_DBUS="no")
    if test "$HAVE_DBUS" = "yes"; then
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
    CPPFLAGS="$old_CPPFLAGS"
    LIBS="$old_LIBS"
  else
    AC_MSG_RESULT([no])
  fi
fi

dnl GNOME Keyring -------------------

AC_ARG_WITH(gnome_keyring,
  AS_HELP_STRING([--with-gnome-keyring], 
                 [Enable use of GNOME Keyring for auth credentials]),
                 [with_gnome_keyring="$withval"],
                 [with_gnome_keyring=no])

AC_MSG_CHECKING([whether to look for GNOME Keyring])
if test "$with_gnome_keyring" != "no"; then
  AC_MSG_RESULT([yes])
  if test "$enable_shared" = "yes"; then
    if test "$APR_HAS_DSO" = "yes"; then
      if test -n "$PKG_CONFIG"; then
        if test "$HAVE_DBUS" = "yes"; then
          AC_MSG_CHECKING([for GLib and GNOME Keyring .pc files])
          if $PKG_CONFIG --exists glib-2.0 gnome-keyring-1; then
            AC_MSG_RESULT([yes])
            old_CPPFLAGS="$CPPFLAGS"
            SVN_GNOME_KEYRING_INCLUDES="$DBUS_CPPFLAGS `$PKG_CONFIG --cflags glib-2.0 gnome-keyring-1`"
            CPPFLAGS="$CPPFLAGS $SVN_GNOME_KEYRING_INCLUDES"
            AC_CHECK_HEADER(gnome-keyring.h, with_gnome_keyring=yes, with_gnome_keyring=no)
            AC_MSG_CHECKING([for GNOME Keyring])
            if test "$with_gnome_keyring" = "yes"; then
              AC_MSG_RESULT([yes])
              AC_DEFINE([SVN_HAVE_GNOME_KEYRING], [1], 
                        [Is GNOME Keyring support enabled?])
              CPPFLAGS="$old_CPPFLAGS"
              SVN_GNOME_KEYRING_LIBS="$DBUS_LIBS `$PKG_CONFIG --libs glib-2.0 gnome-keyring-1`"
            else
              AC_MSG_RESULT([no])
              AC_MSG_ERROR([cannot find GNOME Keyring])
            fi
          else
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([cannot find GLib and GNOME Keyring .pc files])
          fi
        else
          AC_MSG_ERROR([cannot find D-Bus])
        fi
      else
        AC_MSG_ERROR([cannot find pkg-config])
      fi
    else
      AC_MSG_ERROR([APR does not have support for DSOs])
    fi
  else
    AC_MSG_ERROR([--with-gnome-keyring conflicts with --disable-shared])
  fi
else
  AC_MSG_RESULT([no])
fi
AC_SUBST(SVN_GNOME_KEYRING_INCLUDES)
AC_SUBST(SVN_GNOME_KEYRING_LIBS)


dnl I18n -------------------

AC_ARG_ENABLE(nls,
  AS_HELP_STRING([--disable-nls],[Disable gettext functionality]),
  [enable_nls=$enableval],[enable_nls=yes])

USE_NLS="no"
if test "$enable_nls" = "yes"; then
  dnl First, check to see if there is a working msgfmt.
  AC_PATH_PROG(MSGFMT, msgfmt, none)
  AC_PATH_PROG(MSGMERGE, msgmerge, none)
  AC_PATH_PROG(XGETTEXT, xgettext, none)
  if test "$MSGFMT" != "none"; then
    AC_SEARCH_LIBS(bindtextdomain, [intl], [],
                   [
                    AC_MSG_WARN([bindtextdomain() not found.  Disabling NLS.])
                    enable_nls="no"
                   ])
    if test "$enable_nls" = "yes"; then
      AC_DEFINE(ENABLE_NLS, 1,
                [Define to 1 if translation of program messages to the user's
                 native language is requested.])
      USE_NLS="yes"
    fi
  fi
fi

AH_BOTTOM(
#define N_(x) x
#define U_(x) x
#ifdef ENABLE_NLS
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext(PACKAGE_NAME, x)
#define Q_(x1, x2, n) dngettext(PACKAGE_NAME, x1, x2, n)
#else
#define _(x) (x)
#define Q_(x1, x2, n) (((n) == 1) ? x1 : x2)
#define gettext(x) (x)
#define dgettext(domain, x) (x)
#endif
)

dnl Used to simulate makefile conditionals.
GETTEXT_CODESET=\#
NO_GETTEXT_CODESET=\#
if test $USE_NLS = "yes"; then
   AC_CHECK_FUNCS(bind_textdomain_codeset,
                  [ GETTEXT_CODESET="" ],
                  [ NO_GETTEXT_CODESET="" ])
fi
AC_SUBST(GETTEXT_CODESET)
AC_SUBST(NO_GETTEXT_CODESET)

# Check if we are using GNU gettext.
GNU_GETTEXT=no
MSGFMTFLAGS=''
if test $USE_NLS = "yes"; then
   AC_MSG_CHECKING(if we are using GNU gettext)
   if $MSGFMT --version 2>&1 | $EGREP GNU > /dev/null; then
      GNU_GETTEXT=yes
      MSGFMTFLAGS='-c'
   fi
   AC_MSG_RESULT($GNU_GETTEXT)
fi
AC_SUBST(MSGFMTFLAGS)


dnl KWallet -------------------
SVN_LIB_KWALLET

if test "$svn_lib_kwallet" = "yes"; then
  AC_DEFINE([SVN_HAVE_KWALLET], 1,
            [Defined if KWallet support is enabled])
fi


dnl Build and install rules -------------------

INSTALL_STATIC_RULES="install-bin install-docs"
INSTALL_RULES="install-fsmod-lib install-ramod-lib install-lib install-include install-static"
INSTALL_RULES="$INSTALL_RULES $INSTALL_APACHE_RULE"
BUILD_RULES="fsmod-lib ramod-lib lib bin test $BUILD_APACHE_RULE"

if test "$svn_lib_berkeley_db" = "yes"; then
  BUILD_RULES="$BUILD_RULES bdb-lib bdb-test"
  INSTALL_RULES="`echo $INSTALL_RULES | sed 's/install-fsmod-lib/install-fsmod-lib install-bdb-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-bdb-lib"
  BDB_TEST_DEPS="\$(BDB_TEST_DEPS)"
  BDB_TEST_PROGRAMS="\$(BDB_TEST_PROGRAMS)"
fi
  
if test "$svn_lib_neon" = "yes"; then
  BUILD_RULES="$BUILD_RULES neon-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | sed 's/install-ramod-lib/install-ramod-lib install-neon-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-neon-lib"
fi

if test "$svn_lib_serf" = "yes"; then
  BUILD_RULES="$BUILD_RULES serf-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | sed 's/install-ramod-lib/install-ramod-lib install-serf-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-serf-lib"
fi

if test "$svn_lib_kwallet" = "yes"; then
  BUILD_RULES="$BUILD_RULES kwallet-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | sed 's/install-lib/install-lib install-kwallet-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-kwallet-lib"
fi

if test "$with_gnome_keyring" = "yes"; then
  BUILD_RULES="$BUILD_RULES gnome-keyring-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | sed 's/install-lib/install-lib install-gnome-keyring-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-gnome-keyring-lib"
fi

if test "$USE_NLS" = "yes"; then
  BUILD_RULES="$BUILD_RULES locale"
  INSTALL_RULES="$INSTALL_RULES install-locale"
fi

AC_SUBST(BUILD_RULES)
AC_SUBST(INSTALL_STATIC_RULES)
AC_SUBST(INSTALL_RULES)
AC_SUBST(BDB_TEST_DEPS)
AC_SUBST(BDB_TEST_PROGRAMS)

dnl Check for header files ----------------

dnl Standard C headers
AC_HEADER_STDC

dnl Check for typedefs, structures, and compiler characteristics ----------

dnl if compiler doesn't understand `const', then define it empty
AC_C_CONST

dnl if non-existent, define size_t to be `unsigned'
AC_TYPE_SIZE_T


dnl Check for library functions ----------

dnl libsvn_string uses memcmp()
AC_FUNC_MEMCMP

dnl svn_error's default warning handler uses vfprintf()
AC_FUNC_VPRINTF

dnl check for functions needed in special file handling
AC_CHECK_FUNCS(symlink readlink)


dnl Process some configuration options ----------

AC_ARG_WITH(ssl,
AS_HELP_STRING([--with-ssl],
               [This option does NOT affect the Subversion build process in any
                way. It enables OpenSSL support in the Neon library. If and
                only if you are building Neon as an integrated part of the
                Subversion build process, rather than linking to an already
                installed version of Neon, you probably want to pass this
                option so that Neon (and so indirectly, Subversion) will be
                capable of https:// access.]),
[])

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [Turn on debugging]),
[
    if test "$enableval" = "yes" ; then
      enable_debugging="yes"
    else
      enable_debugging="no"
    fi
],
[
    # Neither --enable-debug nor --disable-debug was passed.
    enable_debugging="maybe"

        # We leave this as 'maybe' because setting it to 'no' would modify the
        # CFLAGS/CXXFLAGS later (just after the --enable-maintainer-mode block),
        # and we don't want to force that.
    #
    # TODO: set enable_debugging='no' here, and provide another way to disable
    #       the CFLAGS/CXXFLAGS munging.
])


AC_ARG_ENABLE(maintainer-mode,
AS_HELP_STRING([--enable-maintainer-mode],
               [Turn on debugging and very strict compile-time warnings]),
[
    if test "$enableval" = "yes" ; then
      if test "$enable_debugging" = "no" ; then
        AC_MSG_ERROR(Can't have --disable-debug and --enable-maintainer-mode)
      fi
      enable_debugging=yes
      if test "$GCC" = "yes"; then
        AC_MSG_NOTICE([maintainer-mode: adding GCC warning flags])
        dnl Enable some extra warnings. Put these before the user's flags
        dnl so the user can specify flags that override these.
        CFLAGS="-Wpointer-arith -Wwrite-strings -Wshadow -ansi -Wall -Wformat=2 -Wunused -Waggregate-return -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wno-multichar -Wredundant-decls -Wnested-externs -Wunreachable-code -Winline -Wno-long-long $CFLAGS"
        CXXFLAGS="-Wpointer-arith -Wwrite-strings -Wshadow $CXXFLAGS"

        dnl some additional flags that can be handy for an occasional review,
        dnl but throw too many warnings in svn code, of too little importance,
        dnl to keep these enabled. Remove the "dnl" to do a run with these
        dnl switches enabled.
        dnl CFLAGS="-Wswitch-enum -Wswitch-default $CFLAGS"

        dnl Add each of the following flags only if the compiler accepts it.

        CFLAGS_KEEP="$CFLAGS"
        AC_LANG_PUSH([C])

        CFLAGS="-Wextra-tokens $CFLAGS_KEEP"
        AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])], [CFLAGS_KEEP="$CFLAGS"])

        CFLAGS="-Wnewline-eof $CFLAGS_KEEP"
        AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])], [CFLAGS_KEEP="$CFLAGS"])

        CFLAGS="-Wshorten-64-to-32 $CFLAGS_KEEP"
        AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])], [CFLAGS_KEEP="$CFLAGS"])

        CFLAGS="-Wold-style-definition $CFLAGS_KEEP"
        AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])], [CFLAGS_KEEP="$CFLAGS"])

        AC_LANG_POP([C])
        CFLAGS="$CFLAGS_KEEP"
      fi
    fi
])

if test "$enable_debugging" = "yes" ; then
  dnl At the moment, we don't want optimization, because we're
  dnl debugging.
  CFLAGS=["`echo $CFLAGS' ' | sed -e 's/-O[^ ]* //g'`"]
  CXXFLAGS=["`echo $CXXFLAGS' ' | sed -e 's/-O[^ ]* //g'`"]
  dnl SVN_DEBUG enables specific features for developer builds
  dnl AP_DEBUG enables specific (Apache) features for developer builds
  CFLAGS="$CFLAGS -DSVN_DEBUG -DAP_DEBUG"
  CXXFLAGS="$CXXFLAGS -DSVN_DEBUG -DAP_DEBUG"
elif test "$enable_debugging" = "no" ; then
    CFLAGS=["`echo $CFLAGS' ' | sed -e 's/-g[0-9] //g' | sed -e 's/-g//g'`"]
    CXXFLAGS=["`echo $CXXFLAGS' ' | sed -e 's/-g[0-9] //g' | sed -e 's/-g//g'`"]
    dnl Compile with NDEBUG to get rid of assertions
    CFLAGS="$CFLAGS -DNDEBUG"
    CXXFLAGS="$CXXFLAGS -DNDEBUG"
# elif test "$enable_debugging" = "maybe" ; then
#   # do nothing
fi


AC_ARG_WITH(editor,
AS_HELP_STRING([--with-editor=PATH],
               [Specify a default editor for the subversion client.]),
[

    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-editor requires an argument.])
    else
      SVN_CLIENT_EDITOR=$withval
      AC_DEFINE_UNQUOTED(SVN_CLIENT_EDITOR, "$SVN_CLIENT_EDITOR",
                         [The path of a default editor for the client.])

    fi

])

SVN_LIB_Z

MOD_ACTIVATION="-a"
AC_ARG_ENABLE(mod-activation,
AS_HELP_STRING([--disable-mod-activation],
               [Do not enable mod_dav_svn in httpd.conf]),
[
    if test "$enableval" = "no" ; then
      MOD_ACTIVATION=""
      AC_MSG_NOTICE([Disabling apache module activation])
    else
      AC_MSG_NOTICE([Enabling apache module activation])
    fi
])
AC_SUBST(MOD_ACTIVATION)



AC_ARG_ENABLE(gprof,
AS_HELP_STRING([--enable-gprof],
               [Produce gprof profiling data in 'gmon.out' (GCC only).]),
[
    if test "$enableval" = "yes" ; then
      dnl Probably other compilers support -pg or something similar;
      dnl feel free to extend this to include them.
      if test "$GCC" = "yes"; then
        if test "$enable_shared" = "yes" ; then
          AC_MSG_ERROR(Can't have --enable-gprof without --disable-shared (we
                  recommend also using --enable-all-static).)
        fi
        if test ! "$enable_all_static" = "yes" ; then
          AC_MSG_WARN(We recommend --enable-all-static with --enable-gprof.)
        fi
        AC_MSG_NOTICE([Enabling gprof profiling data (to gmon.out).])
        CFLAGS="$CFLAGS -pg"
        CXXFLAGS="$CXXFLAGS -pg"
        LT_LDFLAGS="$LT_LDFLAGS -pg"
      else
        AC_MSG_ERROR(We only support --enable-gprof with GCC right now.)
      fi
    fi
])


# Scripting and Bindings languages

# Python: Used for testsuite, and bindings


PYTHON="`$abs_srcdir/build/find_python.sh`"
if test -z "$PYTHON"; then
  AC_MSG_WARN([Python 2.4 or later is required to run the testsuite])
  AC_MSG_WARN([or to use the Subversion Python bindings])
  AC_MSG_WARN([])
  AC_MSG_WARN([If you have a suitable Python installed, but not on the])
  AC_MSG_WARN([PATH, set the environment variable PYTHON to the full path])
  AC_MSG_WARN([to the Python executable, and re-run configure])
fi
AC_PATH_PROGS(PYTHON, "$PYTHON", none)

# The minimum version for the JVM runtime for our Java bytecode.
JAVA_OLDEST_WORKING_VER='1.2'
# SVN_CHECK_JDK sets $JAVA_CLASSPATH
SVN_CHECK_JDK($JAVA_OLDEST_WORKING_VER)

AC_PATH_PROG(PERL, perl, none)

AC_PATH_PROG(RUBY, ruby, none)
if test "$RUBY" != "none"; then
  if "$RUBY" -r mkmf -e 'exit(have_func("rb_hash_foreach") ? 0 : 1)'; then
    AC_PATH_PROG(RDOC, rdoc, none)
    AC_CACHE_CHECK([for Ruby major version], [svn_cv_ruby_major],[
    svn_cv_ruby_major="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(MAJOR))'`"
    ])
    RUBY_MAJOR="$svn_cv_ruby_major"

    AC_CACHE_CHECK([for Ruby minor version], [svn_cv_ruby_minor],[
    svn_cv_ruby_minor="`$RUBY -rrbconfig -e 'print Config::CONFIG.fetch(%q(MINOR))'`"
    ])
    RUBY_MINOR="$svn_cv_ruby_minor"

    AC_SUBST(RUBY_MAJOR)
    AC_SUBST(RUBY_MINOR)
  else
    RUBY="none"
    AC_MSG_WARN([The detected Ruby is too old for Subversion to use])
    AC_MSG_WARN([A Ruby which has rb_hash_foreach is required to use the])
    AC_MSG_WARN([Subversion Ruby bindings])
    AC_MSG_WARN([Upgrade to the official 1.8.2 release, or later])
  fi
fi

SVN_CHECK_SWIG

SVN_CHECK_CTYPESGEN

dnl decide whether we want to link against the RA/FS libraries
AC_ARG_ENABLE(runtime-module-search,
AS_HELP_STRING([--enable-runtime-module-search],
               [Turn on dynamic loading of RA/FS libraries]),
[
    if test "$enableval" = "yes"; then
      use_dso=yes
      if test "$enable_shared" = "no"; then
        AC_MSG_ERROR([--enable-runtime-module-search conflicts with --disable-shared])
      fi
      AC_DEFINE(SVN_USE_DSO, 1,
                [Defined if svn should try to load DSOs])
    fi
])

if test "$enable_shared" = "no" || test "$use_dso" != "yes"; then
  ### We want ra_local first to handle the following scenario:
  ### user has neon and db3 in /usr/local, and db4 in
  ### /usr/local/BerkeleyDB.4.0.  If libsvn_ra_neon.la comes before 
  ### libsvn_fs.la then libtool ends up linking libsvn_fs to the db3
  ### in /usr/local/lib
  AC_DEFINE(SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL, 1,
      [Defined if libsvn_client should link against libsvn_ra_local])
  svn_ra_lib_deps="\$(RA_LOCAL_DEPS)"
  svn_ra_lib_install_deps="install-ramod-lib"
  svn_ra_lib_link="\$(RA_LOCAL_LINK)"

  AC_DEFINE(SVN_LIBSVN_CLIENT_LINKS_RA_SVN, 1,
      [Defined if libsvn_client should link against libsvn_ra_svn])
  svn_ra_lib_deps="$svn_ra_lib_deps \$(RA_SVN_DEPS)"
  svn_ra_lib_link="$svn_ra_lib_link \$(RA_SVN_LINK)"

  if test "$svn_lib_neon" = "yes"; then
    AC_DEFINE(SVN_LIBSVN_CLIENT_LINKS_RA_NEON, 1,
          [Defined if libsvn_client should link against libsvn_ra_neon])
    svn_ra_lib_deps="$svn_ra_lib_deps \$(RA_NEON_DEPS)"
    svn_ra_lib_install_deps="$svn_ra_lib_install_deps install-neon-lib"
    svn_ra_lib_link="$svn_ra_lib_link \$(RA_NEON_LINK)"
  fi

  if test "$svn_lib_serf" = "yes"; then
    AC_DEFINE(SVN_LIBSVN_CLIENT_LINKS_RA_SERF, 1,
          [Defined if libsvn_client should link against libsvn_ra_serf])
    svn_ra_lib_deps="$svn_ra_lib_deps \$(RA_SERF_DEPS)"
    svn_ra_lib_install_deps="$svn_ra_lib_install_deps install-serf-lib"
    svn_ra_lib_link="$svn_ra_lib_link \$(RA_SERF_LINK)"
  fi

  SVN_RA_LIB_DEPS=$svn_ra_lib_deps
  SVN_RA_LIB_INSTALL_DEPS=$svn_ra_lib_install_deps
  SVN_RA_LIB_LINK=$svn_ra_lib_link

  AC_DEFINE(SVN_LIBSVN_FS_LINKS_FS_FS, 1,
      [Defined if libsvn_fs should link against libsvn_fs_fs])
  svn_fs_lib_deps="\$(FS_FS_DEPS)"
  svn_fs_lib_install_deps="install-fsmod-lib"
  svn_fs_lib_link="\$(FS_FS_LINK)"

  if test "$svn_lib_berkeley_db" = "yes"; then
  AC_DEFINE(SVN_LIBSVN_FS_LINKS_FS_BASE, 1,
      [Defined if libsvn_fs should link against libsvn_fs_base])
    svn_fs_lib_deps="$svn_fs_lib_deps \$(FS_BASE_DEPS)"
    svn_fs_lib_install_deps="$svn_fs_lib_install_deps install-bdb-lib"
    svn_fs_lib_link="$svn_fs_lib_link \$(FS_BASE_LINK)"
  fi

  SVN_FS_LIB_DEPS=$svn_fs_lib_deps
  SVN_FS_LIB_INSTALL_DEPS=$svn_fs_lib_install_deps
  SVN_FS_LIB_LINK=$svn_fs_lib_link
fi

AC_SUBST(SVN_RA_LIB_DEPS)
AC_SUBST(SVN_RA_LIB_INSTALL_DEPS)
AC_SUBST(SVN_RA_LIB_LINK)
AC_SUBST(SVN_FS_LIB_DEPS)
AC_SUBST(SVN_FS_LIB_INSTALL_DEPS)
AC_SUBST(SVN_FS_LIB_LINK)

# ==== JavaHL ================================================================

dnl Possibly compile JavaHL
do_javahl_build=no
AC_ARG_ENABLE(javahl,
  AS_HELP_STRING([--enable-javahl],
             [Enable compilation of Java high-level bindings (requires C++)]),
  [ if test "$enableval" = "yes" ; then
        do_javahl_build="yes"
    fi
  ])

JAVAHL_OBJDIR=""
INSTALL_EXTRA_JAVAHL_LIB=""
FIX_JAVAHL_LIB=""
JAVAHL_TESTS_TARGET=""
LT_CXX_LIBADD=""
if test "$do_javahl_build" = "yes"; then
  dnl Check for suitable JDK
  if test "$JDK_SUITABLE" = "no"; then
    AC_MSG_ERROR([Cannot compile JavaHL without a suitable JDK. 
                  Please specify a suitable JDK using the --with-jdk option.])
  fi
  
  dnl The temporary directory where libtool compiles libsvnjavahl.
  dnl N.B. objdir is "the name of the directory that contains temporary
  dnl libtool files" and it is set by libtool.m4
  JAVAHL_OBJDIR='$(libsvnjavahl_PATH)'/$objdir
  
  os_arch=`uname`
  if test "$os_arch" = "Darwin"; then
    dnl On Darwin, JNI libs must be installed as .jnilib
    INSTALL_EXTRA_JAVAHL_LIB='ln -sf $(libdir)/libsvnjavahl-1.dylib $(libdir)/libsvnjavahl-1.jnilib'
    FIX_JAVAHL_LIB="ln -sf libsvnjavahl-1.dylib $JAVAHL_OBJDIR/libsvnjavahl-1.jnilib"
  fi
  # This segment (and the rest of r10800) is very likely unnecessary
  # with libtool 1.5, which automatically adds libstdc++ as a
  # dependency to the C++ libraries it builds.  So at some future time
  # when autogen.sh requires libtool 1.5 or higher, we can get rid of
  # it.
  AC_MSG_CHECKING([for additional flags to link C++ libraries])
  if test "x$ac_compiler_gnu" = "xyes"; then
    LT_CXX_LIBADD="-lstdc++"
    AC_MSG_RESULT([$LT_CXX_LIBADD])
  else
    AC_MSG_RESULT([none needed])
  fi
fi
AC_SUBST(INSTALL_EXTRA_JAVAHL_LIB)
AC_SUBST(JAVAHL_OBJDIR)
AC_SUBST(FIX_JAVAHL_LIB)
AC_SUBST(LT_CXX_LIBADD)

AC_ARG_WITH(junit,
AS_HELP_STRING([--with-junit=PATH],
               [Specify a path to the junit JAR file.]),
[
    if test "$withval" != "no"; then
      if test -n "$JAVA_CLASSPATH"; then
        JAVA_CLASSPATH="$withval:$JAVA_CLASSPATH"
      else
        JAVA_CLASSPATH="$withval"
      fi
      JAVAHL_TESTS_TARGET="javahl-tests"
    fi
])
AC_SUBST(JAVA_CLASSPATH)
AC_SUBST(JAVAHL_TESTS_TARGET)

# ==== Miscellaneous bits ====================================================

dnl Since this is used only on Unix-y systems, define the path separator as '/'
AC_DEFINE_UNQUOTED(SVN_PATH_LOCAL_SEPARATOR, '/',
        [Defined to be the path separator used on your local filesystem])

AC_DEFINE_UNQUOTED(SVN_NULL_DEVICE_NAME, "/dev/null",
        [Defined to be the null device for the system])

DEFAULT_FS_TYPE="fsfs"
AC_DEFINE_UNQUOTED(DEFAULT_FS_TYPE, "$DEFAULT_FS_TYPE",
                   [The fs type to use by default])

# BSD/OS (BSDi) needs to use a different include syntax in Makefile
INCLUDE_OUTPUTS="include \$(top_srcdir)/build-outputs.mk"
case "$host" in
  *bsdi*)
    # Check whether they've installed GNU make
    if ! make --version > /dev/null 2>&1; then 
      # BSDi make
      INCLUDE_OUTPUTS=".include \"\$(top_srcdir)/build-outputs.mk\""
    fi
    ;;
esac
AC_SUBST(INCLUDE_OUTPUTS)

# ==== Detection complete - output and run config.status =====================

AC_CONFIG_HEADERS(subversion/svn_private_config.h)
AC_CONFIG_COMMANDS([svn_private_config.h],
                   [sed -e "s/@SVN_DB_HEADER@/$SVN_DB_HEADER/" subversion/svn_private_config.h > subversion/svn_private_config.h.new
                    mv -f subversion/svn_private_config.h.new subversion/svn_private_config.h],
                   [SVN_DB_HEADER="$SVN_DB_HEADER"])
AC_CONFIG_FILES([Makefile])

SVN_CONFIG_SCRIPT(tools/backup/hot-backup.py)
SVN_CONFIG_SCRIPT(contrib/client-side/svn_load_dirs/svn_load_dirs.pl)
SVN_CONFIG_SCRIPT(contrib/client-side/svncopy/svncopy.pl)
SVN_CONFIG_SCRIPT(contrib/client-side/svncopy/testsvncopy.pl)
SVN_CONFIG_SCRIPT(contrib/hook-scripts/commit-email.pl)
SVN_CONFIG_SCRIPT(tools/hook-scripts/commit-access-control.pl)
SVN_CONFIG_SCRIPT(subversion/bindings/swig/perl/native/Makefile.PL)
SVN_CONFIG_SCRIPT(packages/solaris/pkginfo)
AC_SUBST(SVN_CONFIG_SCRIPT_FILES)

AC_OUTPUT

# ==== Print final messages to user ==========================================

dnl Configure is long - users tend to miss warnings printed during it.
dnl Hence, print a warnings about what we did and didn't configure at the 
dnl end, where people will actually see them.

case "$svn_lib_berkeley_db" in
  "no" )
    db_version="$SVN_FS_WANT_DB_MAJOR.$SVN_FS_WANT_DB_MINOR.$SVN_FS_WANT_DB_PATCH"
    AC_MSG_WARN([we have configured without BDB filesystem support


You don't seem to have Berkeley DB version $db_version or newer
installed and linked to APR-UTIL.  We have created Makefiles which
will build without the Berkeley DB back-end; your repositories will
use FSFS as the default back-end.  You can find the latest version of
Berkeley DB here:
  http://www.oracle.com/technology/software/products/berkeley-db/index.html
])
;;
esac

