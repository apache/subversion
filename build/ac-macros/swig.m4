dnl check to see if SWIG is current enough.
dnl
dnl if it is, then check to see if we have the correct version of python.
dnl
dnl if we do, then set up the appropriate SWIG_ variables to build the 
dnl python bindings.

AC_DEFUN(SVN_CHECK_SWIG,
[
  AC_ARG_ENABLE(swig-bindings,
                AC_HELP_STRING([--enable-swig-bindings=LIST],
                               [Build swig bindings for LIST targets only. 
                                LIST is a comma separated list of targets
                                or 'all' for all available targets; currently
                                (java,) perl and python are supported
                                (default=all)]),
  [
    case "$enableval" in
      "yes")
         SWIG_BINDINGS_ENABLE(all)
      ;;
      "no")
      ;;
      *)
         SWIG_BINDINGS_ENABLE($enableval)
      ;;
    esac
  ],
  [
    SWIG_BINDINGS_ENABLE(all)
  ])

  AC_ARG_WITH(swig,
              AC_HELP_STRING([--with-swig=PATH],
                             [Try to use 'PATH/bin/swig' to build the
                              swig bindings.  If PATH is not specified,
                              look for a 'swig' binary in your PATH.]),
  [
    case "$withval" in
      "no")
        SWIG_SUITABLE=no
      ;;
      "yes")
        SVN_FIND_SWIG(check)
      ;;
      *)
        SVN_FIND_SWIG($withval)
      ;;
    esac
  ],
  [
    SVN_FIND_SWIG(check)
  ])
])

AC_DEFUN(SWIG_BINDINGS_ENABLE,
[
  bindings=$1

  if test "$bindings" = "all"; then
    bindings="perl,python,java"
  fi

  for binding in `echo "$bindings" | sed -e "s/,/ /g"`; do
    eval "svn_swig_bindings_enable_$binding='yes'"
    AC_MSG_NOTICE([Enabled swig binding: $binding])
  done
])

AC_DEFUN(SVN_FIND_SWIG,
[
  where=$1

  if test $where = check; then
    AC_PATH_PROG(SWIG, swig, none)
  else
    SWIG=$where/bin/swig
  fi

  if test "$SWIG" != "none"; then
    AC_MSG_CHECKING([swig version])
    SWIG_VERSION_RAW="`$SWIG -version 2>&1 | \
                       sed -ne 's/^.*Version \(.*\)$/\1/p'`"
    # We want the version as an integer so we can test against
    # which version we're using.  SWIG doesn't provide this 
    # to us so we have to come up with it on our own.  
    # The major is passed straight through,
    # the minor is zero padded to two places,
    # and the patch level is zero padded to three places.
    # e.g. 1.3.21 becomes 103021
    SWIG_VERSION="`echo \"$SWIG_VERSION_RAW\" | \
                  sed -e 's/[[^0-9\.]].*$//' \
                      -e 's/\.\([[0-9]]\)$/.0\1/' \
                      -e 's/\.\([[0-9]][[0-9]]\)$/.0\1/' \
                      -e 's/\.\([[0-9]]\)\./0\1/; s/\.//g;'`"
    AC_MSG_RESULT([$SWIG_VERSION_RAW])
    AC_SUBST(SWIG_VERSION)
    # If you change the required swig version number, don't forget to update:
    #   subversion/bindings/swig/INSTALL
    #   subversion/bindings/swig/README
    #   packages/rpm/mandrake-9.0/subversion.spec
    #   packages/rpm/redhat-7.x/subversion.spec
    #   packages/rpm/redhat-8.x/subversion.spec
    if test -n "$SWIG_VERSION" && test "$SWIG_VERSION" -ge "103019"; then
        SWIG_SUITABLE=yes
        AC_CACHE_CHECK([for swig library directory], [ac_cv_swig_swiglib_dir],[
                        ac_cv_swig_swiglib_dir="`$SWIG -swiglib`"
                       ])
        SWIG_LIBSWIG_DIR="$ac_cv_swig_swiglib_dir"

      dnl Newer versions of SWIG have deprecated the -c "do not
      dnl include SWIG runtime functions (used for creating multi-module
      dnl packages)" in favor of the -noruntime flag.
      if test "$SWIG_VERSION" -ge "103020"; then
          SWIG_NORUNTIME_FLAG='-noruntime'
      else
          SWIG_NORUNTIME_FLAG='-c'
      fi
    else
        SWIG_SUITABLE=no
        AC_MSG_WARN([swig bindings version 1.3.19 or newer needed for swig support.])
    fi

    if test "$PYTHON" != "none" -a "$SWIG_SUITABLE" = "yes" -a "$svn_swig_bindings_enable_python" = "yes"; then
      AC_MSG_NOTICE("Configuring python swig binding")
      SWIG_BUILD_RULES="$SWIG_BUILD_RULES swig-py-lib"
      SWIG_INSTALL_RULES="$SWIG_INSTALL_RULES install-swig-py-lib"

      AC_CACHE_CHECK([if swig needs -L for its libraries],
        [ac_cv_swig_ldflags],[
        # The swig libraries are one directory above the
        # `swig -swiglib` directory.
        ac_cv_swig_ldflags=""
        swig_lib_dir="`dirname $ac_cv_swig_swiglib_dir`"
        if test "$swig_lib_dir" &&
           test "$swig_lib_dir" != "/lib" &&
           test "$swig_lib_dir" != "/usr/lib"; then
          ac_cv_swig_ldflags="-L$swig_lib_dir"
        fi
      ])
      SWIG_LDFLAGS="$ac_cv_swig_ldflags"

      AC_CACHE_CHECK([for Python includes], [ac_cv_python_includes],[
        ac_cv_python_includes="`$PYTHON ${abs_srcdir}/build/get-py-info.py --includes`"
      ])
      SWIG_PY_INCLUDES="\$(SWIG_INCLUDES) $ac_cv_python_includes"

      AC_CACHE_CHECK([for compiling Python extensions], [ac_cv_python_compile],[
        ac_cv_python_compile="`$PYTHON ${abs_srcdir}/build/get-py-info.py --compile`"
      ])
      SWIG_PY_COMPILE="$ac_cv_python_compile"

      AC_CACHE_CHECK([for linking Python extensions], [ac_cv_python_link],[
        ac_cv_python_link="`$PYTHON ${abs_srcdir}/build/get-py-info.py --link`"
      ])
      SWIG_PY_LINK="$ac_cv_python_link"
    fi

    if test "$JDK" != "none" -a "$SWIG_SUITABLE" = "yes" -a "$svn_swig_bindings_enable_java" = "yes"; then
      ### TODO: enable when functional
#      SWIG_BUILD_RULES="$SWIG_BUILD_RULES swig-java-lib"
#      SWIG_INSTALL_RULES="$SWIG_INSTALL_RULES install-swig-java-lib"

      SWIG_JAVA_INCLUDES='-I$(JDK)/include'
      list="`find "$JDK/include" -type d -print`"
      for dir in $list; do
        SWIG_JAVA_INCLUDES="$SWIG_JAVA_INCLUDES -I$dir"
      done
      dnl For now, use the compile and link from python as a base.
      SWIG_JAVA_COMPILE="$SWIG_PY_COMPILE"
      dnl To relink our generated native binding libraries against
      dnl libsvn_swig_java, we must include the latter's library path.
      dnl ### Eventually reference somewhere under $(DESTDIR)?
      SWIG_JAVA_LINK="$SWIG_PY_LINK -L\$(SWIG_BUILD_DIR)/.libs"
    fi

    if test "$PERL" != "none" -a "$SWIG_SUITABLE" = "yes" -a "$svn_swig_bindings_enable_perl" = "yes"; then
      ### TODO: enable when the target is implemented correctly
#      SWIG_BUILD_RULES="$SWIG_BUILD_RULES swig-pl-lib"
#      SWIG_INSTALL_RULES="$SWIG_INSTALL_RULES install-swig-pl-lib"
      SWIG_PL_INCLUDES="\$(SWIG_INCLUDES) `$PERL -MExtUtils::Embed -e ccopts`"
      SWIG_PL_COMPILE="`$PERL -MConfig -e 'print $Config{cc}'` \$(SWIG_PL_INCLUDES)"
      SWIG_PL_LINK="`$PERL -MConfig -e 'print $Config{ld}'` `$PERL -MConfig -e 'print $Config{lddlflags}'` `$PERL -MExtUtils::Embed -e ldopts`"
    fi

  fi
  AC_SUBST(SWIG_BUILD_RULES)
  AC_SUBST(SWIG_INSTALL_RULES)
  AC_SUBST(SWIG_NORUNTIME_FLAG)
  AC_SUBST(SWIG_PY_INCLUDES)
  AC_SUBST(SWIG_PY_COMPILE)
  AC_SUBST(SWIG_PY_LINK)
  AC_SUBST(SWIG_JAVA_INCLUDES)
  AC_SUBST(SWIG_JAVA_COMPILE)
  AC_SUBST(SWIG_JAVA_LINK)
  AC_SUBST(SWIG_PL_INCLUDES)
  AC_SUBST(SWIG_PL_COMPILE)
  AC_SUBST(SWIG_PL_LINK)
  AC_SUBST(SWIG_LIBSWIG_DIR)
  AC_SUBST(SWIG_LDFLAGS)
])
