#!/bin/sh
#
# Helper script to generate the ctypesgen wrappers
#

LT_EXECUTE="$1"

CPPFLAGS="$2"
EXTRA_CTYPES_LDFLAGS="$3"
PYTHON="$4"
CTYPESGEN="$5"

abs_srcdir="$6"
abs_builddir="$7"

svn_libdir="$8"
apr_prefix="$9"
apu_prefix="${10}"

cp_relpath="subversion/bindings/ctypes-python"
output="$cp_relpath/svn_all.py"

if test "$abs_builddir" = "$abs_srcdir"; then
  svn_includes="subversion/include"
else
  svn_includes="$abs_srcdir/subversion/include"
fi

### most of this should be done at configure time and passed in
apr_config="$apr_prefix/bin/apr-1-config"
apu_config="$apr_prefix/bin/apu-1-config"

apr_cppflags="`$apr_config --includes --cppflags`"
apr_include_dir="`$apr_config --includedir`"
apr_ldflags="`$apr_config --ldflags --link-ld`"

apu_cppflags="`$apu_config --includes`"  # no --cppflags
apu_include_dir="`$apu_config --includedir`"
apu_ldflags="`$apu_config --ldflags --link-ld`"

cpp="`$apr_config --cpp`"
### end

cppflags="$apr_cppflags $apu_cppflags -I$svn_includes"
ldflags="$apr_ldflags $apu_ldflags -L$svn_libdir $EXTRA_CTYPES_LDFLAGS"


# This order is important. The resulting stubs will load libraries in
# this particular order.
### maybe have gen-make do this for us
for lib in subr diff delta fs repos wc ra client ; do
  ldflags="$ldflags -lsvn_$lib-1"
done

includes="$svn_includes/svn_*.h $apr_include_dir/ap[ru]_*.h"
if test "$apr_include_dir" != "$apu_include_dir" ; then
  includes="$includes $apu_include_dir/ap[ru]_*.h"
fi

CPPFLAGS="`echo $CPPFLAGS`"
cppflags="`echo $cppflags`"

echo $LT_EXECUTE $PYTHON $CTYPESGEN --cpp "$cpp $CPPFLAGS $cppflags" $ldflags $includes -o $output --no-macro-warnings --strip-build-path=$abs_srcdir
$LT_EXECUTE $PYTHON $CTYPESGEN --cpp "$cpp $CPPFLAGS $cppflags" $ldflags $includes -o $output --no-macro-warnings --strip-build-path=$abs_srcdir

(cat $abs_srcdir/$cp_relpath/csvn/core/functions.py.in; \
 sed -e '/^FILE =/d' \
     -e 's/restype = POINTER(svn_error_t)/restype = SVN_ERR/' $output \
 ) > $abs_builddir/$cp_relpath/csvn/core/functions.py
