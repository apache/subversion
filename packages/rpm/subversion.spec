Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: M5
Release: @RELEASE@
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-%{version}-%{release}.tar.gz
Patch0: expat.patch
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: apr >= 2001.10.24
Requires: db3 >= 3.3.11
Requires: expat
Requires: libxml
BuildPreReq: apr-devel >= 2001.10.24
BuildPreReq: autoconf >= 2.52
BuildPreReq: db3-devel >= 3.3.11
BuildPreReq: expat-devel
BuildPreReq: libtool >= 1.4.2
BuildPreReq: libxml-devel
BuildPreReq: neon >= 0.17.2
BuildPreReq: python
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

%prep
%setup -q
%patch -p1
sh autogen.sh
rm -rf expat-lite
./configure --prefix=/usr

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
