%define apache_version 2.0.47-0.1
%define neon_version 0.23.9
%define apache_dir /usr/local/apache2
# If you don't have 360+ MB of free disk space or don't want to run checks then
# set make_*_check to 0.
%define make_ra_local_check 1
%define make_ra_svn_check 1
%define make_ra_dav_check 1
%define make_cvs2svn_check 1
Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: @VERSION@
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
SOURCE0: subversion-%{version}-%{release}.tar.gz
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: apache-libapr >= %{apache_version}
Requires: db4 >= 4.0.14
Requires: neon >= %{neon_version}
Requires: python2
Obsoletes: subversion-cvs2svn
BuildPreReq: apache >= %{apache_version}
BuildPreReq: apache-devel >= %{apache_version}
BuildPreReq: apache-libapr-devel >= %{apache_version}
BuildPreReq: autoconf >= 2.53
BuildPreReq: db4-devel >= 4.0.14
BuildPreReq: docbook-style-xsl >= 1.58.1
BuildPreReq: doxygen
BuildPreReq: expat-devel
BuildPreReq: libtool >= 1.4.2-12
BuildPreReq: libxslt >= 1.0.27
BuildPreReq: neon-devel >= %{neon_version}
BuildPreReq: openssl-devel
BuildPreReq: python2
BuildPreReq: python2-devel
BuildPreReq: swig >= 1.3.16
BuildPreReq: swig-runtime >= 1.3.16
BuildPreReq: zlib-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Prefix: /usr
%description
Subversion is a concurrent version control system which enables one or more
users to collaborate in developing and maintaining a hierarchy of files and
directories while keeping a history of all changes.  Subversion only stores
the differences between versions, instead of every complete file.  Subversion
also keeps a log of who, when, and why changes occurred.

As such it basically does the same thing CVS does (Concurrent Versioning System)
but has major enhancements compared to CVS and fixes a lot of the annoyances
that CVS users face.

*** Note: This is a relocatable package; it can be installed anywhere you like
with the "rpm -Uvh --prefix /your/favorite/path" command. This is useful
if you don't have root access on your machine but would like to use this
package.

%package devel
Group: Utilities/System
Summary: Development package for Subversion developers.
Requires: subversion = %{version}-%{release}
Requires: apache >= %{apache_version}
%description devel
The subversion-devel package includes the static libraries and include files
for developers interacting with the subversion package.

%package server
Group: Utilities/System
Summary: Apache server module for Subversion server.
Requires: apache >= %{apache_version}
Requires: apache-libapr >= 0.%{apache_version}
Requires: subversion = %{version}-%{release}
BuildPreReq: apache-devel >= %{apache_version}
%description server
The subversion-server package adds the Subversion server Apache module to
the Apache directories and configuration.

%package python
Group: Utilities/System
Summary: Allows Python scripts to directly use Subversion repositories.
Requires: swig-runtime >= 1.3.16
Requires: python2
Obsoletes: subversion-cvs2svn
%description python
Provides Pythong (SWIG) support for Subversion.

%package tools
Group: Utilities/System
Summary: Tools for Subversion
%description tools
Tools for Subversion.

%changelog
* Sat Jul 19 2003 David Summers <david@summersoft.fay.ar.us> 0.25.0-6515
- Added cvs2svn regression tests.

* Sat Jul 19 2003 David Summers <david@summersoft.fay.ar.us> 0.25.0-6514
- PORTING file no longer exists.
- Thanks to Ralph Loader <suckfish@ihug.co.nz> for the following changes:
- gdbm-devel is not a build pre-req for subversion but APR/APR-UTIL.
- LDFLAGS no longer needed when compiling without subversion-devel installed.
- Use %configure instead of ./configure.
- expat is not a direct dependency of subversion.
- No need to copy separate subversion.conf and httpd.davcheck.conf as they
  are in the source tree, just reference them there.
- Simplify "make install" by making use of DESTDIR macro instead of setting
  individual directory components.

* Sun Jul 13 2003 David Summers <david@summersoft.fay.ar.us> 0.25.0-6462
- Fix revision number to be properly generated during RPM build.  Can't use
  the normal svnversion command at this point because the SRPM is not a
  repository that we can get the version from via svnversion command.

* Sun Jul 13 2003 David Summers <david@summersoft.fay.ar.us> 0.25.0-6461
- Fix install/un-install not to bomb out on update if apache (httpd) doesn't
  restart correctly.

* Thu Jul 10 2003 David Summers <david@summersoft.fay.ar.us> 0.25.0-6434
- Apache 2.0.47 now recommended because of security issues.

* Tue Jun 24 2003 David Summers <david@summersoft.fay.ar.us> 0.24.2-6334
- Now requires apache 2.0.46.

* Mon Jun 16 2003 David Summers <david@summersoft.fay.ar.us> 0.24.1-6256
- Added doxygen documentation.

* Tue Jun 10 2003 David Summers <david@summersoft.fay.ar.us> 0.23.0-6188
- Track changes for addition of mod_authz_svn httpd module.

* Sat Jun 07 2003 David Summers <david@summersoft.fay.ar.us> 0.23.0-6163
- svn-design.info is no longer built.

* Sat May 24 2003 David Summers <david@summersoft.fay.ar.us> 0.23.0-6036
- Track changes to Python SWIG build.
- Now requires neon-0.23.9 to pick up bug and security fixes.
- Now builds the book and puts it in /usr/share/doc/subversion-VERSION/book
  directory.  RedHat 7.x and RedHat 8.x users who build this RPM will need to
  install or upgrade to the RedHat 9.0 docbook-style-xsl and libxslt packages.

* Thu May 15 2003 David Summers <david@summersoft.fay.ar.us> 0.22.2-5943
- The subversion package now requires python 2 because cvs2svn has been
  unswigified and just depends on python 2.
- The new subversion-python package requires python 2.

* Sat May 10 2003 David Summers <david@summersoft.fay.ar.us> 0.22.1-5879
- svn-config has been taken back out of the distribution.
- cvs2svn no longer requires SWIG, so rename the subversion-cvs2svn package to
  subversion-python and move the cvs2svn and RCS parser into the subversion
  package.
- Added cvs2svn man page.

* Sun Apr 13 2003 David Summers <david@summersoft.fay.ar.us> 0.20.1-5610
- Added svndumpfilter.

* Thu Apr 03 2003 David Summers <david@summersoft.fay.ar.us> 0.20.1-5542
- Updated to apache-2.0.45-0.1.
- Took out libsvn_auth as it is no longer generated or used.

* Sat Mar 01 2003 David Summers <david@summersoft.fay.ar.us> 0.18.1-5173
- Enabled RA_DAV checking.
  Now requires httpd package to build because of RA_DAV tests.

* Sat Jan 18 2003 David Summers <david@summersoft.fay.ar.us> 0.16.1-4433
- Created tools package to hold the tools.

* Thu Jan 16 2003 David Summers <david@summersoft.fay.ar.us> 0.16.1-4405
- Now requires apache >= 2.0.44-0.1 (APACHE_2_0_BRANCH) which contains the new
  version of APR/APR-UTILS as of 2003.01.15.
- Added svnversion command.

* Tue Dec 31 2002 David Summers <david@summersoft.fay.ar.us> 0.16.0-4218
- Create a svnadmin.static which is copied to svnadmin-version-release
  when the package is erased, so users can still dump/load their repositories
  even after they have upgraded the RPM package.

* Sun Dec 29 2002 David Summers <david@summersoft.fay.ar.us> 0.16.0-4206
- Switched to new db4 package to be more like RedHat 8.0.
- Switched to new version of apache that combines APR and APRUTILS into one
  package.

* Sat Dec 14 2002 David Summers <david@summersoft.fay.ar.us> 0.16.0-4128
- SWIG now builds so we can use cvs2svn.

* Fri Oct 04 2002 David Summers <david@summersoft.fay.ar.us> 0.14.3-3280
- Made cvs2svn conditional (at least until we can get it to build consistently
  and work).

* Sat Sep 21 2002 David Summers <david@summersoft.fay.ar.us> 0.14.3-3205
- Added SWIG dependencies to add cvs2svn capabilities.

* Fri Aug 16 2002 David Summers <david@summersoft.fay.ar.us> 0.14.1-2984
- Now requires neon-0.22.0.

* Thu Aug 15 2002 David Summers <david@summersoft.fay.ar.us> 0.14.1-2978
- Took out loading mod_dav_svn from subversion.spec file and put it in
  subversion.conf file which goes into the apache conf directory.
- Simplify what gets put into httpd.conf to only the include for the
  subversion.conf file.
  (Thanks to Scott Harrison <sharrison@users.sourceforge.net> for prompting
  me to do this).

* Thu Aug 08 2002 David Summers <david@summersoft.fay.ar.us> 0.14.0-2919
- Updated to APR/APR-UTIL 2002-08-08.

* Tue Jun 25 2002 David Summers <david@summersoft.fay.ar.us> 0.13.0-2332
- Updated to APACHE/APR/APR-UTIL 2002-06-25.
- Previous version had a few problems because of missing apache error/ files.

* Sun Jun 23 2002 David Summers <david@summersoft.fay.ar.us> 0.13.0-2318
- Updated to apache-2.0.40-0.3.
- Updated to subversion-0.13.1-2318.

* Tue Jun 18 2002 David Summers <david@summersoft.fay.ar.us> 0.13.0-2277
- Updated for RedHat 7.3 (autoconf253).
- Added a bunch of pre-requisites I didn't know were needed because I built a
  new machine that didn't have them already installed.
- Fixed installation of man and info documentation pages.

* Wed Mar 06 2002 David Summers <david@summersoft.fay.ar.us> 0.9.0-1447
- Back to apache-libapr* stuff, hopefully to stay.

* Sun Feb 24 2002 David Summers <david@summersoft.fay.ar.us> 0.9.0-1373
- Fixed expat.patch to not have to make so many changes by writing a small
  shell script that changes libexpat to -lexpat.

* Fri Feb 22 2002 Blair Zajac <blair@orcaware.com> 0.9.0-1364
- Updated to neon-0.19.2.

* Mon Feb 11 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1250
- Back to using apr and apr-util separately from apache.

* Mon Feb 11 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1232
- Updated to APR and APR-UTIL 2002.02.11.
- Updated to apache-2.0.32-0.2. (Requires apache-libapr and apache-libapr-util).
- Took out a (now non-existant) documentation file.
- Moved SPEC file changelog to after all package definitions.
  
* Sun Feb 03 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1153
- Updated to neon-0.18.5.
- Broke up apache and apache-devel into apache-apr, apache-apr-devel,
  apache-apr-utils, and apache-apr-utils-devel.
- Updated apache to APR and APR-UTILS to 2002.02.03 version.

* Sat Feb 02 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1147
- Now builds without the separate APR package as it is built into and
  "exported" from apache-2.0.31-0.3.

* Fri Feb 01 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1132
- Took out patches to install procedure now not required because of fixes
  in rev 1130.

* Fri Feb 01 2002 David Summers <david@summersoft.fay.ar.us> 0.8.0-1129
- Added requirement for APR 0.2002.01.19 rev 2 where the /usr/bin/apr-config
  program was added.

* Sun Oct 28 2001 David Summers <david@summersoft.fay.ar.us>
- Release M5-r340: Added the subversion-server package.

* Fri Oct 26 2001 David Summers <david@summersoft.fay.ar.us>
- Release M5-r327: No longer need expat-lite. We can use the normal expat.

* Thu Sep 27 2001 David Summers <david@summersoft.fay.ar.us>
- Release M3-r117: Initial Version.

%prep
%setup -q

if [ -f /usr/bin/autoconf-2.53 ]; then
   AUTOCONF="autoconf-2.53"
   AUTOHEADER="autoheader-2.53"
   export AUTOCONF AUTOHEADER
fi
sh autogen.sh


# Fix up mod_dav_svn installation.
patch -p1 < packages/rpm/redhat-7.x/install.patch

# Fix documentation version generation.
patch -p1 < packages/rpm/redhat-7.x/doc.patch

# Brand release number into the displayed version number.
RELEASE_NAME="r%{release}"
export RELEASE_NAME
vsn_file="subversion/include/svn_version.h"
sed -e \
 "/#define *SVN_VER_TAG/s/dev build/${RELEASE_NAME}/" \
  < "$vsn_file" > "${vsn_file}.tmp"
mv "${vsn_file}.tmp" "$vsn_file"

# Configure static.
%configure \
	--without-berkeley-db \
	--disable-shared \
	--enable-all-static \
	--with-swig \
	--with-python=/usr/bin/python2.2 \
	--with-apxs=%{apache_dir}/bin/apxs \
	--with-apr=%{apache_dir}/bin/apr-config \
	--with-apr-util=%{apache_dir}/bin/apu-config

%build
# Make svnadmin static.
make subversion/svnadmin/svnadmin

# Move static svnadmin to safe place.
cp subversion/svnadmin/svnadmin svnadmin.static

# Configure shared.
%configure \
	--with-swig \
	--with-python=/usr/bin/python2.2 \
	--with-apxs=%{apache_dir}/bin/apxs \
	--with-apr=%{apache_dir}/bin/apr-config \
	--with-apr-util=%{apache_dir}/bin/apu-config

# Make everything shared.
make clean
make

make swig-py

%if %{make_ra_local_check}
echo "*** Running regression tests on RA_LOCAL (FILE SYSTEM) layer ***"
make check
echo "*** Finished regression tests on RA_LOCAL (FILE SYSTEM) layer ***"
%endif

%if %{make_ra_svn_check}
echo "*** Running regression tests on RA_SVN (SVN method) layer ***"
killall lt-svnserve || true
sleep 1
./subversion/svnserve/svnserve -d -r `pwd`/subversion/tests/clients/cmdline/
make svncheck
killall lt-svnserve
echo "*** Finished regression tests on RA_SVN (SVN method) layer ***"
%endif

%if %{make_ra_dav_check}
echo "*** Running regression tests on RA_DAV (HTTP method) layer ***"
killall httpd || true
sleep 1
cp -f /usr/local/apache2/bin/httpd .
sed -e "s;@SVNDIR@;`pwd`;" < packages/rpm/redhat-7.x/httpd.davcheck.conf > httpd.conf
./httpd -f `pwd`/httpd.conf
sleep 1
make check BASE_URL='http://localhost:15835'
killall httpd
echo "*** Finished regression tests on RA_DAV (HTTP method) layer ***"
%endif

# Build python bindings
make swig-py

%if %{make_cvs2svn_check}
(
echo "*** Running regression tests on cvs2svn ***"
cd tools/cvs2svn
./run-tests.py
)
%endif

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{apache_dir}/conf
make install DESTDIR="$RPM_BUILD_ROOT"

# Add subversion.conf configuration file into httpd.conf directory.
cp packages/rpm/redhat-7.x/subversion.conf $RPM_BUILD_ROOT/%{apache_dir}/conf

# Install cvs2svn and supporting files
make install-swig-py DESTDIR=$RPM_BUILD_ROOT DISTUTIL_PARAM=--prefix=$RPM_BUILD_ROOT
sed -e 's;#!/usr/bin/env python;#!/usr/bin/env python2;' < $RPM_BUILD_DIR/%{name}-%{version}/tools/cvs2svn/cvs2svn.py > $RPM_BUILD_ROOT/usr/bin/cvs2svn
chmod a+x $RPM_BUILD_ROOT/usr/bin/cvs2svn
mkdir -p $RPM_BUILD_ROOT/usr/lib/python2.2/site-packages
cp -r tools/cvs2svn/rcsparse $RPM_BUILD_ROOT/usr/lib/python2.2/site-packages/rcsparse
mv $RPM_BUILD_ROOT/usr/lib/svn-python/* $RPM_BUILD_ROOT/usr/lib/python2.2/site-packages
rmdir $RPM_BUILD_ROOT/usr/lib/svn-python
cp $RPM_BUILD_DIR/subversion-%{version}/tools/cvs2svn/cvs2svn.1 $RPM_BUILD_ROOT/usr/share/man/man1

# Copy svnadmin.static to destination
cp svnadmin.static $RPM_BUILD_ROOT/usr/bin/svnadmin-%{version}-%{release}.static

# Set up tools package files.
mkdir -p $RPM_BUILD_ROOT/usr/lib/subversion
cp -r tools $RPM_BUILD_ROOT/usr/lib/subversion
# Don't need to include 29+ Meg cvs2svn test stuff.
rm -rf $RPM_BUILD_ROOT/usr/lib/subversion/tools/cvs2svn

# Set up book generation and installation
(cd doc/book;
rm -f book/version.xml
make SVNVERSION=%{release} XSL_DIR=/usr/share/sgml/docbook/xsl-stylesheets all-html)
cp -r doc/book/book/html-chunk book
cp -r doc/book/book/images     book/images

# Create doxygen documentation.
doxygen doc/doxygen.conf

%preun
# Save current copy of svnadmin.static
echo "Saving current svnadmin-%{version}-%{release}.static as svnadmin-%{version}-%{release}."
echo "Erase this program only after you make sure you won't need to dump/reload"
echo "any of your repositories to upgrade to a new version of the database."
cp /usr/bin/svnadmin-%{version}-%{release}.static /usr/bin/svnadmin-%{version}-%{release}

%post server
# Load subversion server into apache configuration.
CONF=%{apache_dir}/conf/httpd.conf

# Search for Subversion include file and add it if not found.

if [ "`grep -i subversion.conf $CONF`"x = "x" ]; then
   cat >> $CONF <<EOF

# Begin Subversion server configuration - Please don't delete this line.
include conf/subversion.conf
# End Subversion server configuration - Please don't delete this line.
EOF
fi

# Restart apache server if needed.
source /etc/init.d/functions
if [ "`pidof httpd2`"x != "x" ]; then
   /etc/init.d/httpd2 restart || true
fi

%preun server
# Take subversion configuration out of apache configuration file.
# Only take it out if this package is being erased and not upgraded.
if [ "$1" = "0" ];
   then
   cd %{apache_dir}/conf && sed -e '/^# Begin Subversion server/,/^# End Subversion server/s/^/#/' < httpd.conf > httpd.conf.new && mv httpd.conf httpd.conf.bak && mv httpd.conf.new httpd.conf
fi

%postun server
# Restart apache server if needed.
source /etc/init.d/functions
if [ "`pidof httpd`"x != "x" ]; then
   /etc/init.d/httpd2 restart || true
fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc BUGS CHANGES COMMITTERS COPYING HACKING IDEAS INSTALL README
%doc subversion/LICENSE
%doc book
/usr/bin/cvs2svn
/usr/bin/svn
/usr/bin/svnadmin
/usr/bin/svnadmin-%{version}-%{release}.static
/usr/bin/svndumpfilter
/usr/bin/svnlook
/usr/bin/svnserve
/usr/bin/svnversion
/usr/lib/libsvn_client*so*
/usr/lib/libsvn_delta*so*
/usr/lib/libsvn_diff*so*
/usr/lib/libsvn_fs*so*
/usr/lib/libsvn_ra*so*
/usr/lib/libsvn_repos*so*
/usr/lib/libsvn_subr*so*
/usr/lib/libsvn_wc*so*
/usr/lib/python2.2/site-packages/rcsparse
/usr/share/man/man1/*

%files devel
%defattr(-,root,root)
%doc doc/doxygen/html/*
/usr/lib/libsvn*.a
/usr/lib/libsvn*.la
/usr/include/subversion-1

%files server
%defattr(-,root,root)
%config %{apache_dir}/conf/subversion.conf
%{apache_dir}/modules/mod_dav_svn.la
%{apache_dir}/modules/mod_dav_svn.so
%{apache_dir}/modules/mod_authz_svn.la
%{apache_dir}/modules/mod_authz_svn.so

%files python
%defattr(-,root,root)
/usr/lib/python2.2/site-packages/svn
/usr/lib/python2.2/site-packages/libsvn
/usr/lib/libsvn_swig_py*so*

%files tools
%defattr(-,root,root)
/usr/lib/subversion/tools
