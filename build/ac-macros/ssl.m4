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
  SVN_SSL_INCLUDE=""
  svn_lib_ssl=no

  AC_ARG_WITH(ssl, AC_HELP_STRING([--with-ssl], [enable SSL support]))

  if test "$withval" != "no"; then
    AC_MSG_CHECKING([for OpenSSL])
    if echo $CFLAGS | grep -q 'DNEON_SSL'; then
      SVN_SSL_LIBS=$NEON_LIBS
      SVN_SSL_INCLUDE=$SVN_NEON_INCLUDES
      svn_lib_ssl=yes
      AC_MSG_RESULT([using neon's])
    else
      svn_lib_ssl_libs=$LIBS
      LIBS="$LIBS -lssl"
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
        SVN_SSL_LIBS="-lssl"
      fi
      AC_MSG_RESULT([$svn_lib_ssl])
      LIBS=$svn_lib_ssl_libs
    fi
  fi

  AC_SUBST(SVN_SSL_LIBS)
  AC_SUBST(SVN_SSL_INCLUDES)
])



