## Note, pass them the source xml directory, since they can't find it in VPATH
## builds
check:
	sh @srcdir@/svn-test.sh @top_srcdir@/subversion/tests-common/xml
	sh @srcdir@/svn-test2.sh @top_srcdir@/subversion/tests-common/xml

## We need to include these in the distribution
EXTRA_DIST = svn-test.sh svn-test2.sh
