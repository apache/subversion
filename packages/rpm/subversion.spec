%define neon_version 0.17.1
Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: M3
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-%{version}-r%{release}.tar.gz
Source1: apr-2001-09-28.tar.gz
Source2: neon-%{neon_version}.tar.gz
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: db3 >= 3.3.11
BuildPreReq: autoconf >= 2.52, db3-devel >= 3.3.11 libtool >= 1.4.2
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
* Thu Sep 27 2001 David Summers <david@summersoft.fay.ar.us>
- Release M3-r117: Initial Version.

%prep
%setup -q
%setup -q -D -T -a 1
%setup -q -D -T -a 2

mv neon-%{neon_version} neon

sh autogen.sh
./configure --enable-maintainer-mode --disable-shared

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man1
make prefix=$RPM_BUILD_ROOT/usr install

# Install man page until the previous install can do it correctly.
cp ./subversion/clients/cmdline/man/svn.1 $RPM_BUILD_ROOT/usr/share/man/man1

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc AUTHORS BUGS COPYING HACKING IDEAS INSTALL NEWS PORTING
%doc README STACK TASKS
%doc doc notes tools subversion/LICENSE
/usr/lib/lib*
/usr/bin/svn
/usr/bin/svnadmin
/usr/bin/svnlook
/usr/share/man/man1/*
