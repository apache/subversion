%define apache_version 2.0.43-0.1
%define neon_version 0.23.2
%define apache_dir /usr/local/apache2
# If you don't have 360+ MB of free disk space or don't want to run checks then
# set make_check to 0.
%define make_check 1
# If you don't want to try to build cvs2svn then change build_cvs2svn to 0
%define build_cvs2svn 1
Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: @VERSION@
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-%{version}-%{release}.tar.gz
Source1: subversion.conf
Source2: rcsparse.py
Patch0: install.patch
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: apache-libapr >= %{apache_version}
Requires: apache-libapr-utils >= %{apache_version}
Requires: db >= 4.0.14
Requires: expat
Requires: neon >= %{neon_version}
#Requires: /sbin/install-info
BuildPreReq: apache >= %{apache_version}
BuildPreReq: apache-devel >= %{apache_version}
BuildPreReq: apache-libapr-devel >= %{apache_version}
BuildPreReq: apache-libapr-utils-devel >= %{apache_version}
BuildPreReq: autoconf253 >= 2.53
BuildPreReq: db-devel >= 4.0.14
BuildPreReq: expat-devel
BuildPreReq: gdbm-devel
BuildPreReq: libtool >= 1.4.2
BuildPreReq: neon-devel >= %{neon_version}
BuildPreReq: openssl-devel
BuildPreReq: python2
BuildPreReq: python2-devel
%if %{build_cvs2svn}
BuildPreReq: swig >= 1.3.16
%endif
BuildPreReq: texinfo
BuildPreReq: zlib-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Prefix: /usr
%description
Subversion is a concurrent version control system which enables one or more
users to collaborate in developing and maintaining a hierarchy of files and
directories while keeping a history of all changes.  Subversion only stores
the differences between versions, instead of every complete file.  Subversion
also keeps a log of who, when, and why changes occured.

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
Requires: apache-libapr >= 0.%{apache_version}
Requires: apache-libapr-utils >= 0.%{apache_version}
Requires: subversion = %{version}-%{release}
BuildPreReq: apache-devel >= %{apache_version}
%description server
The subversion-server package adds the Subversion server Apache module to
the Apache directories and configuration.

%if %{build_cvs2svn}
%package cvs2svn
Group: Utilities/System
Summary: Converts CVS repositories to Subversion repositories.
Requires: swig-runtime >= 1.3.16
%description cvs2svn
Converts CVS repositories to Subversion repositories.

See /usr/share/doc/subversion*/tools/cvs2svn directory for more information.

%endif

%changelog
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

LDFLAGS="-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_client/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_delta/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_fs/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_repos/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra_dav/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra_local/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra_svn/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_subr/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_wc/.libs \
	" ./configure \
	--prefix=/usr \
%if %{build_cvs2svn}
	--with-swig \
	--with-python=/usr/bin/python2.2 \
%endif
	--with-apxs=%{apache_dir}/bin/apxs \
	--with-apr=%{apache_dir}/bin/apr-config \
	--with-apr-util=%{apache_dir}/bin/apu-config

# Fix up mod_dav_svn installation.
%patch0 -p1

%build
make

make swig-py-ext

%if %{make_check}
make check
%endif

%if %{build_cvs2svn}
# Build cvs2svn python bindings
cd subversion/bindings/swig/python
/usr/bin/python2 setup.py build
%endif

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/share
mkdir -p $RPM_BUILD_ROOT/%{apache_dir}/conf
make install \
	prefix=$RPM_BUILD_ROOT/usr \
	mandir=$RPM_BUILD_ROOT/usr/share/man \
	fs_libdir=$RPM_BUILD_ROOT/usr/lib \
	base_libdir=$RPM_BUILD_ROOT/usr/lib \
	infodir=$RPM_BUILD_ROOT/usr/share/info \
	libexecdir=$RPM_BUILD_ROOT/%{apache_dir}/lib

%if %{build_cvs2svn}
make install-swig-py-ext DISTUTIL_PARAM=--prefix=$RPM_BUILD_ROOT/usr
%endif

# Add subversion.conf configuration file into httpd.conf directory.
cp %{SOURCE1} $RPM_BUILD_ROOT/%{apache_dir}/conf

%if %{build_cvs2svn}
# Install cvs2svn and supporting files
cd subversion/bindings/swig/python
/usr/bin/python2 setup.py install --prefix $RPM_BUILD_ROOT/usr
sed -e 's;#!/usr/bin/env python;#!/usr/bin/env python2;' < $RPM_BUILD_DIR/%{name}-%{version}/tools/cvs2svn/cvs2svn.py > $RPM_BUILD_ROOT/usr/bin/cvs2svn
chmod a+x $RPM_BUILD_ROOT/usr/bin/cvs2svn
cp %{SOURCE2} $RPM_BUILD_ROOT/usr/lib/python2.2/site-packages
%endif

%post
# Only add to INFO directory if this is the only instance installed.
if [ "$1"x = "1"x ]; then
   if [ -x /sbin/install-info ]; then
      /sbin/install-info /usr/share/info/svn-design.info.gz \
         /usr/share/info/dir \
         --entry='* Subversion-design: (svn-design).          Subversion Versioning System Design Manual'
   fi
fi

%preun
# Only delete from INFO directory if this is the last instance being deleted.
if [ "$1"x = "0"x ]; then
   if [ -x /sbin/install-info ]; then
      /sbin/install-info --delete /usr/share/info/svn-design.info.gz \
         /usr/share/info/dir \
         --entry='* Subversion-design: (svn-design).          Subversion Versioning System Design Manual'
   fi
fi

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
   /etc/init.d/httpd2 restart
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
   /etc/init.d/httpd2 restart
fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc BUGS COMMITTERS COPYING HACKING IDEAS INSTALL PORTING README
%doc tools subversion/LICENSE
/usr/bin/svn
/usr/bin/svnadmin
/usr/bin/svnlook
/usr/bin/svnserve
/usr/lib/libsvn_auth*so*
/usr/lib/libsvn_client*so*
/usr/lib/libsvn_delta*so*
/usr/lib/libsvn_fs*so*
/usr/lib/libsvn_ra*so*
/usr/lib/libsvn_repos*so*
/usr/lib/libsvn_subr*so*
/usr/lib/libsvn_wc*so*
/usr/share/man/man1/*
/usr/share/info/*

%files devel
%defattr(-,root,root)
/usr/lib/libsvn*.a
/usr/lib/libsvn*.la
/usr/include/subversion-1
/usr/bin/svn-config

%files server
%defattr(-,root,root)
%config %{apache_dir}/conf/subversion.conf
%{apache_dir}/modules/mod_dav_svn.la
%{apache_dir}/modules/mod_dav_svn.so

%if %{build_cvs2svn}
%files cvs2svn
%defattr(-,root,root)
/usr/bin/cvs2svn
/usr/lib/python2.2/site-packages/svn
/usr/lib/python2.2/site-packages/rcsparse.py
/usr/lib/libsvn_swig_py*so*
%endif
