#!/bin/sh
#
# copy-swig-py.sh: copy the Python bindings' .py files to the install locn
#
# USAGE: copy-swig-py.sh PYTHON INSTALL SOURCE_DIR DEST_DIR
#

if test "$#" != 4; then
  echo USAGE: $0 PYTHON INSTALL SOURCE_DIR DEST_DIR
  exit 1
fi

pyprog="$1"
install="$2"
srcdir="$3"
dstdir="$4"

# cd to the source directory so that we can get filenames rather than paths
cd "$srcdir"

# copy all the .py files to the target location
for file in *.py; do
  $install "$file" "${dstdir}/${file}"
done

# figure out where the precompiling script is located
script="`\"$pyprog\" -c 'import compileall; print compileall.__file__'`"

# precompile all of the files
"$pyprog" "$script" "$dstdir"
