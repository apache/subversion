%define neon_version 0.17.2
Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: M5
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-%{version}-%{release}.tar.gz
Patch0: expat.patch
Patch1: install.patch
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: apr >= 2001.10.24
Requires: db3 >= 3.3.11
Requires: expat
Requires: libxml
Requires: neon = %{neon_version}
Requires: w3c-libwww
BuildPreReq: apr-devel >= 2001.10.24
BuildPreReq: autoconf >= 2.52
BuildPreReq: db3-devel >= 3.3.11
BuildPreReq: expat-devel
BuildPreReq: libtool >= 1.4.2
BuildPreReq: libxml-devel
BuildPreReq: neon = %{neon_version}
BuildPreReq: python
BuildPreReq: w3c-libwww-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Prefix: /usr
%description
Subversion does the same thing CVS does (Concurrent Versioning System) but has
major enhancements compared to CVS.

*** Note: This is a relocatable package; it can be installed anywhere you like
with the "rpm -Uvh --prefix /your/favorite/path" command. This is useful
if you don't have root access on your machine but would like to use this
package.

%changelog
* Sun Oct 28 2001 David Summers <david@summersoft.fay.ar.us>
- Release M5-r340: Added the subversion-server package.

* Fri Oct 26 2001 David Summers <david@summersoft.fay.ar.us>
- Release M5-r327: No longer need expat-lite. We can use the normal expat.

* Thu Sep 27 2001 David Summers <david@summersoft.fay.ar.us>
- Release M3-r117: Initial Version.

%package devel
Group: Utilities/System
Summary: Development package for Subversion developers.
%description devel
The subversion-devel package includes the static libraries and include files
for developers interacing with the subversion package.

%package server
Group: Utilities/System
Summary: Apache server module for Subversion server.
Requires: apache-devel >= 2.0.16
Requires: subversion
BuildPreReq: apache-devel >= 2.0.16
%description server
The subversion-server package adds the Subversion server Apache module to
the Apache directories and configuration.

%prep
%setup -q

# Fix up expat library.
%patch0 -p1

sh autogen.sh
rm -rf expat-lite
./configure --prefix=/usr

# Fix up mod_dav_svn installation.
%patch1 -p1

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man1
make prefix=$RPM_BUILD_ROOT/usr libexecdir=$RPM_BUILD_ROOT/usr/lib/apache install

# Install man page until the previous install can do it correctly.
cp ./subversion/clients/cmdline/man/svn.1 $RPM_BUILD_ROOT/usr/share/man/man1

%post server
# Load subversion server into apache configuration.
cd /usr/lib/apache
/usr/sbin/apxs -i -a -n dav_svn libmod_dav_svn.la

# Conditionally add subversion example configuration.
if [ "`grep -i svnpath /etc/httpd/conf/httpd.conf`"x = "x" ]; then
   cat >> /etc/httpd/conf/httpd.conf <<EOF

# Begin Subversion server configuration - Please don't delete this line.
#<Location /svn/repos>
#   DAV svn
#   SVNPath /home/svnroot
#</Location>
# End Subversion server configuration - Please don't delete this line.
EOF
fi

# Restart apache server if needed.
source /etc/init.d/functions
if [ "`pidof httpd`"x != "x" ]; then
   /etc/init.d/httpd restart
fi

%preun server
# Take subversion configuration out of apache configuration file.
cd /etc/httpd/conf && sed -e 's/^LoadModule dav_svn_module/#LoadModule dav_svn_module/' -e '/^# Begin Subversion server/,/^# End Subversion server/s/^/#/' < httpd.conf > httpd.conf.new && mv httpd.conf httpd.conf.bak && mv httpd.conf.new httpd.conf

%postun server
# Restart apache server if needed.
source /etc/init.d/functions
if [ "`pidof httpd`"x != "x" ]; then
   /etc/init.d/httpd restart
fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc BUGS COMMITTERS COPYING HACKING IDEAS INSTALL NEWS PORTING
%doc README STACK TASKS
%doc doc notes tools subversion/LICENSE
/usr/bin/svn
/usr/bin/svnadmin
/usr/bin/svnlook
/usr/lib/libsvn*so*
/usr/share/man/man1/*

%files devel
%defattr(-,root,root)
/usr/lib/libsvn*.a
/usr/lib/libsvn*.la
/usr/include/svn*

%files server
%defattr(-,root,root)
/usr/lib/apache/libmod_dav_svn.*
