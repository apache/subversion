#/bin/sh
# script to run the junit test cases for
# the java subversion library
# 
# make sure you compiled everything with:
# - build-java.sh (all of the java classes)
# - build.sh (native classes)
export LD_LIBRARY_PATH=.
export CLASSPATH=$CLASSPATH:tests
java AllTests





