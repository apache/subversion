#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
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
apr_config="$9"
apu_config="${10}"
cpp="${11}"

cp_relpath="subversion/bindings/ctypes-python"
output="$cp_relpath/svn_all.py"

# Avoid build path in csvn/core/functions.py
if test "$abs_builddir" = "$abs_srcdir"; then
  svn_includes="subversion/include"
else
  mkdir -p "$cp_relpath/csvn/core"
  svn_includes="$abs_srcdir/subversion/include"
fi

### most of this should be done at configure time and passed in
apr_cppflags="`$apr_config --includes --cppflags`"
apr_include_dir="`$apr_config --includedir`"
apr_ldflags="`$apr_config --ldflags --link-ld`"

apu_cppflags="`$apu_config --includes`"  # no --cppflags
apu_include_dir="`$apu_config --includedir`"
apu_ldflags="`$apu_config --ldflags --link-ld`"

### end

cppflags="$apr_cppflags $apu_cppflags -I$svn_includes"
ldflags="-L$svn_libdir $apr_ldflags $apu_ldflags $EXTRA_CTYPES_LDFLAGS"


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

# Remove some whitespace in csvn/core/functions.py
CPPFLAGS="`echo $CPPFLAGS`"
cppflags="`echo $cppflags`"

echo $LT_EXECUTE $PYTHON $CTYPESGEN --cpp "$cpp $CPPFLAGS $cppflags" $ldflags $includes -o $output --no-macro-warnings --strip-build-path=$abs_srcdir
$LT_EXECUTE $PYTHON $CTYPESGEN --cpp "$cpp $CPPFLAGS $cppflags" $ldflags $includes -o $output --no-macro-warnings --strip-build-path=$abs_srcdir

(cat $abs_srcdir/$cp_relpath/csvn/core/functions.py.in; \
 sed -e '/^FILE =/d' $output | \
 perl -pe 's{(\s+\w+)\.restype = POINTER\(svn_error_t\)}{\1.restype = POINTER(svn_error_t)\n\1.errcheck = _svn_errcheck}' \
 ) > $abs_srcdir/$cp_relpath/csvn/core/functions.py
