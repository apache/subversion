dnl
dnl SVN_LIB_DIFFUTILS(mergefiles_dir)
dnl
dnl Set of autoconf tests to detect the appropriate diff/diff3 combinations.
dnl 
dnl Looks for a 'diff'/'gdiff' or 'diff3'/'gdiff3'
dnl
dnl This script assumes $1 is a directory that contains particular
dnl 'mine.txt', 'yours.txt', 'older.txt', and 'result.txt' files
dnl needed to test merging.
dnl

AC_DEFUN(SVN_LIB_DIFFUTILS,
[
  AC_ARG_WITH(diffutils,
              AC_HELP_STRING([--with-diffutils=PREFIX],
                             [prefix for installed GNU diffutils]),
  [
    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-diffutils requires an argument.])
    else
      diffutils_path="$withval/bin"
    fi
  ],[
    diffutils_path=$PATH
  ])

  helper_dir=$1

  dnl Check for a diff that supports the -u flag

  input_1=$helper_dir/check-diff-input-1.txt
  input_2=$helper_dir/check-diff-input-2.txt
  output=$helper_dir/check-diff-output.tmp

  dnl Looking for `diff' binaries
  AC_PATH_PROGS(SVN_CLIENT_DIFF, [gdiff diff], [none], [$diffutils_path])

  if test "$SVN_CLIENT_DIFF" = "none" ; then
    SVN_DOWNLOAD_DIFF()
  fi

  dnl Make absolutely sure there's no output file yet.
  rm -f $output

  $SVN_CLIENT_DIFF -u $input_1 $input_2 > $output 2>/dev/null

  dnl If there's an output file with non-zero size, then this
  dnl diff supported the "-u" flag, so we're done.
  if test ! -s $output ; then
    # Clean up
    rm -f $output*
    SVN_DOWNLOAD_DIFF()
  fi

  # Clean up
  rm -f $output*

  dnl Note: How To Test For GNU Diff:
  dnl
  dnl Right now, we only test that diff supports "-u", which is all
  dnl we care about ("svn diff" passes -u by default).  But if we
  dnl someday want pure GNU diff again, it's an easy tweak to make.
  dnl Just use a construction similar to the if-else-fi above, but
  dnl change the condition to:
  dnl
  dnl grep "\\ No newline at end of file" ${output} > /dev/null 2>&1
  dnl
  dnl (There are options to suppress grep's output, but who
  dnl knows how portable they are, so just redirect instead.)
  dnl
  dnl This will test for a non-broken GNU diff, because the
  dnl input files are constructed to set off the special
  dnl handling GNU diff has for files that don't end with \n.
  dnl
  dnl Why would we care?  Well, we used to check very carefully for
  dnl a non-broken version of GNU diff, because at that time we
  dnl used `diff' not `diff3' for updates.  On FreeBSD, there was
  dnl a version of GNU diff that had been modified to remove the
  dnl special support for files that don't end in \n.
  dnl
  dnl Don't ask my why, but somehow I think there's a chance we
  dnl might one day again need GNU diff on the client side.  If that
  dnl ever happens, remember to read this note. :-)
            
  AC_DEFINE_UNQUOTED(SVN_CLIENT_DIFF, "$SVN_CLIENT_DIFF",
                     [Define to be the full path to diff])

  dnl Looking for `diff3' binaries
  AC_PATH_PROGS(SVN_CLIENT_DIFF3, [gdiff3 diff3], [none], [$diffutils_path])

  if test "$SVN_CLIENT_DIFF3" = "none" ; then
    SVN_DOWNLOAD_DIFF3()
  fi

  dnl Check whether diff3 supports --diff-program arg
  AC_MSG_CHECKING([whether diff3 supports --diff-program arg])
  $SVN_CLIENT_DIFF3 --diff-program=$SVN_CLIENT_DIFF ${0} ${0} ${0} > /dev/null 2>&1
  if test "$?" = "0"; then
    AC_MSG_RESULT([yes])
    AC_DEFINE_UNQUOTED(SVN_DIFF3_HAS_DIFF_PROGRAM_ARG, 1,
                       [Defined if diff3 supports the --diff-program argument])
    diff3_arg="--diff-program=$SVN_CLIENT_DIFF"
  else
    AC_MSG_RESULT([no])
    diff3_arg=""
  fi

  $SVN_CLIENT_DIFF3 $diff3_arg -A -m $helper_dir/mine.txt $helper_dir/older.txt $helper_dir/yours.txt > result 2> /dev/null

	# the actual and expected merge result-files should be the same.
  cmp -s $helper_dir/result.txt result
	if test "$?" != "0"; then
    # cleanup
    rm -f result
    SVN_DOWNLOAD_DIFF3()
	fi

  # cleanup
  rm -f result

  AC_DEFINE_UNQUOTED(SVN_CLIENT_DIFF3, "$SVN_CLIENT_DIFF3",
                     [Define to be the full path to diff])
])

AC_DEFUN(SVN_DOWNLOAD_DIFF,
[
AC_MSG_ERROR([Suitable diff not found.

Cannot find a diff that supports the -u flag.
We recommend GNU diff (version 2.7 or later).
You can get it from ftp://ftp.gnu.org/pub/gnu/diffutils.])
])

AC_DEFUN(SVN_DOWNLOAD_DIFF3,
[
AC_MSG_ERROR([Suitable diff3 not found.

Cannot find an unbroken GNU diff3.
Please make sure you have GNU diff (version 2.7 or later) installed.
You can get it from ftp://ftp.gnu.org/pub/gnu/diffutils.

(Note that FreeBSD uses a modified version of GNU diff that is unable
to handle certain types of text files.  Since diff3 uses GNU diff to do
the actual diffing, this effectively breaks diff3 as well.  If you are
using FreeBSD, please install the /usr/ports/textproc/diffutils port.)])
])
