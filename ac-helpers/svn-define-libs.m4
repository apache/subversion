dnl
dnl Macros to define SVN_*_LIBS
dnl
dnl These symbols are used within our makefiles to enable linking with
dnl each of the SVN libraries.
dnl

AC_DEFUN(SVN_DEFINE_LIB,[
  define([sym],[SVN_LIBSVN_]translit($1,'a-z','A-Z')[_LIBS])
  define([name],[libsvn_$1])
  sym=['$(top_builddir)/subversion/]name[/]name[.la']
  AC_SUBST(sym)
  undefine([name])
  undefine([sym])
])

AC_DEFUN(SVN_DEFINE_LIBS,[
  SVN_DEFINE_LIB(client)
  SVN_DEFINE_LIB(delta)
  SVN_DEFINE_LIB(fs)
  SVN_DEFINE_LIB(repos)
  SVN_DEFINE_LIB(ra)
  SVN_DEFINE_LIB(ra_dav)
  SVN_DEFINE_LIB(ra_local)
  SVN_DEFINE_LIB(subr)
  SVN_DEFINE_LIB(wc)
])
