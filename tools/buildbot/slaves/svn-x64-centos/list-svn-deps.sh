#!/bin/sh
# List the versions of all of SVN's dependencies.
# The output is suitable for putting in the buildbot slave's 'info/host'
# file, after a general description of the slave machine.

echo "=== SVN dependencies ==="
DEPS="gcc apr apr-util apr-devel apr-util-devel httpd httpd-devel \
   neon neon-devel python python-devel ruby ruby-devel"
#yum -C list $DEPS
rpm -q ${DEPS} | sort | uniq
# The SQLite version is found by the name of the amalgamation directory,
# which is found in the home dir.  It is also explicitly referenced in the
# './configure' line in 'svnbuild.sh'.
(cd && echo sqlite-3.*[0-9].*[0-9])
echo

echo "=== SVN test dependencies ==="
#rpm -q pysqlite | sort | uniq
echo

JAVA_VER=`java -fullversion 2>&1`
PY_VER=`python -V 2>&1`
RUBY_VER=`ruby --version`
PERL_VER=`perl -v | grep This`
echo "=== interpreters / bindings ==="
echo "Java:   $JAVA_VER"
echo "Python: $PY_VER"
echo "Ruby:   $RUBY_VER"
echo "Perl:   $PERL_VER"
echo

echo "=== BuildBot version ==="
buildbot --version
echo
