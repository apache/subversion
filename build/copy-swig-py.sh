#!/bin/sh
#
# copy-swig-py.sh: copy the Python bindings' .py files to the install locn
#
# USAGE: copy-swig-py.sh PYTHON INSTALL SOURCE_DIR TARGET_DIR DESTDIR
#

if test "$#" != 5; then
  echo USAGE: $0 PYTHON INSTALL SOURCE_DIR TARGET_DIR DESTDIR
fi

pyprog="$1"
install="$2"
srcdir="$3"
instdir="$4"
destdir="$5"

# cd to the source directory so that we can get filenames rather than paths
cd "$srcdir"

# copy all the .py files to the target location
for file in *.py; do
  $install "$file" "${destdir}${instdir}/${file}"
done

# precompile all of the files
"$pyprog" -c "import compileall; compileall.compile_dir(\"${destdir}${instdir}\",1,\"${instdir}\")"
