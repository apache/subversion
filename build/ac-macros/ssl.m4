dnl   SVN_LIB_SSL
dnl
dnl   If we find a useable version, set the shell variable
dnl   `svn_lib_ssl' to `yes'.  Otherwise, set `svn_lib_ssl'
dnl   to `no'.  Either way, substitute SVN_SSL_LIBS and
dnl   SVN_SSL_INCLUDE appropriately.
dnl
dnl   In order to avoid linking to multiple OpenSSL versions,
dnl   we use Neon's configuration if it exists and includes
dnl   SSL support.  That means that SVN_NEON_CONFIG() must
dnl   be called before SVN_LIB_SSL(), if ever.

AC_DEFUN(SVN_LIB_SSL,
[
  SVN_SSL_LIBS=""
  SVN_SSL_INCLUDES=""
  svn_lib_ssl=no

  AC_ARG_WITH(svn-ssl, AC_HELP_STRING([--with-svn-ssl=PREFIX],
                                      [enable SSL in ra_svn and svnserve]))

  if test "$withval" != "no"; then
    AC_MSG_CHECKING([for OpenSSL])

    save_libs=$LIBS
    save_cppflags=$CPPFLAGS
    if test "$withval" != "yes"; then
        TEST_LIBS="-L$prefix"
        TEST_INCLUDES="-I$prefix"
    fi
    TEST_LIBS="$TEST_LIBS -lssl"

    LIBS="$LIBS $TEST_LIBS"
    CPPFLAGS="$CPPFLAGS $TEST_INCLUDES"
    AC_TRY_RUN([
#include <openssl/ssl.h>

int main(int argc, char **argv)
{
   SSL_load_error_strings();
   SSL_library_init();
   return 0;
}
],
               [svn_lib_ssl=yes],
               [svn_lib_ssl=no],
               [svn_lib_ssl=yes])
    if test "$svn_lib_ssl" = "yes"; then
      SVN_SSL_LIBS=$TEST_LIBS
      SVN_SSL_INCLUDES=$TEST_INCLUDES
    fi
    AC_MSG_RESULT([$svn_lib_ssl])
    LIBS=$save_libs
    CPPFLAGS=$save_cppflags
  fi

  AC_SUBST(SVN_SSL_LIBS)
  AC_SUBST(SVN_SSL_INCLUDES)
  if test "$svn_lib_ssl" = "yes"; then
    AC_DEFINE(SVN_HAVE_SSL, 1, [Define if OpenSSL is available.])
  fi
])
