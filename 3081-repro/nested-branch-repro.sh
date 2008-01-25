#!/bin/sh

### Always invoke this script from here, e.g.: "./nested-branch-repro.sh".
###
### Note that if you put early exit() calls in the script, you'll need
### to manually invoke ./k after running this script, to kill the
### svnserve daemon it started up.  If you let the script run to the
### end, though, it will shut down svnserve itself.

### This should be the only line you need to change.
SVNDIR=/home/kfogel/src/issue-3081

SVN=${SVNDIR}/subversion/svn/svn
SVNSERVE=${SVNDIR}/subversion/svnserve/svnserve
SVNADMIN=${SVNDIR}/subversion/svnadmin/svnadmin

# Select an access method.  If svn://, the svnserve setup is
# handled automagically by this script; but if http://, then
# you'll have to configure it yourself first.
# 
# URL=http://localhost/SOMETHING/repos
URL=svn://localhost/repos
# URL=file:///`pwd`/repos

rm -rf repos wc trunk-wc branch-wc b?-wc import-me /tmp/kff.out

${SVNADMIN} create repos

# These are for svnserve only.
echo "[general]" > repos/conf/svnserve.conf
echo "anon-access = none" >> repos/conf/svnserve.conf
echo "auth-access = write" >> repos/conf/svnserve.conf
echo "password-db = passwd" >> repos/conf/svnserve.conf
echo "authz-db = authz" >> repos/conf/svnserve.conf

echo "jtrusted = 12345" >> repos/conf/passwd
echo "juntrusted = 12345" >> repos/conf/passwd

> repos/conf/authz
echo "[/]" >> repos/conf/authz
echo "jtrusted = rw" >> repos/conf/authz
echo "[/trunk]" >> repos/conf/authz
echo "juntrusted = rw" >> repos/conf/authz
echo "[/trunk/A]" >> repos/conf/authz
echo "juntrusted = " >> repos/conf/authz

function announce()
{
   MSG="$*"
   echo -n "ANN: "
   echo "${MSG}"
}

# The server will only be contacted if $URL is svn://foo, of course.
${SVNSERVE} --pid-file svnserve-pid -d -r `pwd`
# And put the kill command in a file, in case need to run it manually.
echo "kill -9 `cat svnserve-pid`" > k
chmod a+rwx k

announce "### Making a Greek Tree for import..."
mkdir import-me
mkdir import-me/trunk
mkdir import-me/tags
mkdir import-me/branches
mkdir import-me/trunk/A
mkdir import-me/trunk/A/B/
mkdir import-me/trunk/A/C/
mkdir import-me/trunk/A/D/
mkdir import-me/trunk/A/B/E/
mkdir import-me/trunk/A/B/F/
mkdir import-me/trunk/A/D/G/
mkdir import-me/trunk/A/D/H/
echo "This is the file 'iota'."        > import-me/trunk/iota
echo "This is the file 'A/mu'."        > import-me/trunk/A/mu
echo "This is the file 'A/B/lambda'."  > import-me/trunk/A/B/lambda
echo "This is the file 'A/B/E/alpha'." > import-me/trunk/A/B/E/alpha
echo "This is the file 'A/B/E/beta'."  > import-me/trunk/A/B/E/beta
echo "This is the file 'A/D/gamma'."   > import-me/trunk/A/D/gamma
echo "This is the file 'A/D/G/pi'."    > import-me/trunk/A/D/G/pi
echo "This is the file 'A/D/G/rho'."   > import-me/trunk/A/D/G/rho
echo "This is the file 'A/D/G/tau'."   > import-me/trunk/A/D/G/tau
echo "This is the file 'A/D/H/chi'."   > import-me/trunk/A/D/H/chi
echo "This is the file 'A/D/H/omega'." > import-me/trunk/A/D/H/omega
echo "This is the file 'A/D/H/psi'."   > import-me/trunk/A/D/H/psi
announce "### Done."
echo ""
announce "### Importing it..."
cd import-me
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       import -q -m "Initial import; r1." ${URL}
cd ..
announce "### Done."
echo ""

${SVN} --username jtrusted --password 12345 --no-auth-cache \
       cp -m "make branch b1; r2" ${URL}/trunk ${URL}/branches/b1
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       cp -m "make b2 from b1; r3" ${URL}/branches/b1 ${URL}/branches/b2

${SVN} --username jtrusted --password 12345 --no-auth-cache \
       co -q ${URL}/trunk trunk-wc
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       co -q ${URL}/branches/b1 b1-wc
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       co -q ${URL}/branches/b2 b2-wc

cd b2-wc
echo "change to pi" >> A/D/G/pi
echo "change to rho" >> A/D/G/rho
echo "change to tau" >> A/D/G/tau
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       ci -m "Change three files on b2; r4."
cd ..

cd b1-wc
${SVN} merge --username jtrusted --password 12345 \
             -r3:4 ${URL}/branches/b2/A/ ./A
${SVN} commit --username jtrusted --password 12345 \
              -m "Commit merge of b2 changes to b1, at A." A
${SVN}  --username jtrusted --password 12345 up
announce "Properties on b1-wc after merging b2 to b1 at A/:"
${SVN} plist -vR .
announce "(done)"
echo "change to iota" >> iota
echo "change to alpha" >> A/B/E/alpha
echo "change to chi" >> A/D/H/chi
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       ci -m "Change three files on b1; r5."
cd ..

cd trunk-wc
announce "merge all b1 changes to trunk:"
${SVN} --username jtrusted --password 12345 --no-auth-cache \
       merge --reintegrate ${URL}/branches/b1
announce "Properties on trunk-wc:"
${SVN} plist -vR .
announce "(done)"
cd ..

# Kill svnserve.
./k
