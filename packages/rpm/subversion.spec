%define apache_version 2.0.40-0.4
%define neon_version 0.21.2
%define apache_dir /usr/local/apache2
Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: @VERSION@
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-%{version}-%{release}.tar.gz
Patch0: install.patch
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: apache-libapr >= %{apache_version}
Requires: apache-libapr-utils >= %{apache_version}
Requires: db >= 4.0.14
Requires: expat
Requires: neon = %{neon_version}
Requires: /sbin/install-info
BuildPreReq: apache >= %{apache_version}
BuildPreReq: apache-devel >= %{apache_version}
BuildPreReq: apache-libapr-devel >= %{apache_version}
BuildPreReq: apache-libapr-utils-devel >= %{apache_version}
BuildPreReq: autoconf253 >= 2.53
BuildPreReq: db-devel >= 4.0.14
BuildPreReq: expat-devel
BuildPreReq: gdbm-devel
BuildPreReq: libtool >= 1.4.2
BuildPreReq: neon = %{neon_version}
BuildPreReq: openssl-devel
BuildPreReq: python
BuildPreReq: texinfo
BuildPreReq: zlib-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Prefix: /usr
%description
Subversion does the same thing CVS does (Concurrent Versioning System) but has
major enhancements compared to CVS.

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
Requires: perl
Requires: subversion = %{version}-%{release}
BuildPreReq: apache-devel >= %{apache_version}
%description server
The subversion-server package adds the Subversion server Apache module to
the Apache directories and configuration.

%changelog
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
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_subr/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_wc/.libs \
	" ./configure \
	--prefix=/usr \
	--with-apxs=%{apache_dir}/usr/bin/apxs \
	--with-apr=%{apache_dir}/bin/apr-config \
	--with-apr-util=%{apache_dir}/bin/apu-config

# Fix up mod_dav_svn installation.
%patch0 -p1

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/share
make install \
	prefix=$RPM_BUILD_ROOT/usr \
	mandir=$RPM_BUILD_ROOT/usr/share/man \
	fs_libdir=$RPM_BUILD_ROOT/usr/lib \
	base_libdir=$RPM_BUILD_ROOT/usr/lib \
	infodir=$RPM_BUILD_ROOT/usr/share/info \
	libexecdir=$RPM_BUILD_ROOT/%{apache_dir}/lib

%post
# Only add to INFO directory if this is the only instance installed.
if [ "$1"x = "1"x ]; then
   /sbin/install-info /usr/share/info/svn-design.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion-design: (svn-design).          Subversion Versioning System Design Manual'

   /sbin/install-info /usr/share/info/svn-manual.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion: (svn-manual).          Subversion Versioning System Manual'

   /sbin/install-info /usr/share/info/svn_for_cvs_users.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion-cvs: (svn_for_cvs_users).          Subversion Versioning System Information for CVS Users'
fi

%preun
# Only delete from INFO directory if this is the last instance being deleted.
if [ "$1"x = "0"x ]; then
   /sbin/install-info --delete /usr/share/info/svn-design.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion-design: (svn-design).          Subversion Versioning System Design Manual'

   /sbin/install-info --delete /usr/share/info/svn-manual.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion: (svn-manual).          Subversion Versioning System Manual'

   /sbin/install-info --delete /usr/share/info/svn_for_cvs_users.info.gz \
      /usr/share/info/dir \
      --entry='* Subversion-cvs: (svn_for_cvs_users).          Subversion Versioning System Information for CVS Users'
fi

%post server
# Load subversion server into apache configuration.
CONF=%{apache_dir}/conf/httpd.conf

# Search for Subversion dav_svn_module and add it to config file if not found.

if [ "`grep -i dav_svn_module $CONF`"x = "x" ]; then
   # Put in LoadModule dav_svn_module line at end of LoadModule section.
   perl -e '
   while ( <> )
      {
      $FirstLoadFound = 1 if ( ! $FirstLoadFound &&
           ( /^LoadModule/ ) );
      $InsertPointFound = 1,
         print "LoadModule dav_svn_module modules/mod_dav_svn.so\n"
         if ( $FirstLoadFound && ! $InsertPointFound &&
              ! ( /^LoadModule/ ) );
      print;
      }
   ' < $CONF > $CONF.new && mv $CONF $CONF.bak && mv $CONF.new $CONF
fi

# Conditionally add subversion example configuration.
if [ "`grep -i svnpath $CONF`"x = "x" ]; then
   cat >> $CONF <<EOF

# Begin Subversion server configuration - Please don't delete this line.
#<Location /svn/repos>
#   DAV svn
#   SVNPath /home/svnroot
#
#   # Limit write permission to list of valid users.
#   <LimitExcept GET PROPFIND OPTIONS REPORT>
#      # Require SSL connection for password protection.
#      # SSLRequireSSL
#
#      AuthType Basic
#      AuthName "Authorization Realm"
#      AuthUserFile /absolute/path/to/passwdfile
#      Require valid-user
#   </LimitExcept>
#</Location>
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
   cd %{apache_dir}/conf && sed -e 's/^LoadModule dav_svn_module/#LoadModule dav_svn_module/' -e '/^# Begin Subversion server/,/^# End Subversion server/s/^/#/' < httpd.conf > httpd.conf.new && mv httpd.conf httpd.conf.bak && mv httpd.conf.new httpd.conf
fi

%postun server
# Restart apache server if needed.
source /etc/init.d/functions
if [ "`pidof httpd`"x != "x" ]; then
   /etc/init.d/httpd2 restart
fi

%clean
#rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc BUGS COMMITTERS COPYING HACKING IDEAS INSTALL PORTING README
%doc tools subversion/LICENSE
/usr/bin/svn
/usr/bin/svnadmin
/usr/bin/svnlook
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

%files server
%defattr(-,root,root)
%{apache_dir}/modules/mod_dav_svn.la
%{apache_dir}/modules/mod_dav_svn.so
