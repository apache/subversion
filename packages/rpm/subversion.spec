Summary: A Concurrent Versioning system similar to but better than CVS.
Name: subversion
Version: M3
Release: 20010927
Copyright: BSD
Group: Utilities/System
URL: http://subversion.tigris.org
Source0: subversion-M3-20010927.tar.gz
Source1: apr-2001-09-28.tar.gz
Source2: neon-0.15.3.tar.gz
Vendor: Summersoft
Packager: David Summers <david@summersoft.fay.ar.us>
Requires: db3 >= 3.3.11
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
%description
Subversion does the same thing CVS does (Concurrent Versioning System) but has
major enhancements compared to CVS.

%changelog
* Thu Sep 27 2001 David Summers <david@summersoft.fay.ar.us>
- Release M3-r117: Initial Version.

%prep
%setup -q
%setup -q -D -T -a 1
%setup -q -D -T -a 2

mv neon-0.15.3 neon

sh autogen.sh
./configure --enable-maintainer-mode --disable-shared

%build
make

%install
rm -rf $RPM_BUILD_ROOT

make prefix=$RPM_BUILD_ROOT/usr install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc AUTHORS BUGS ChangeLog.CVS COPYING HACKING IDEAS INSTALL NEWS PORTING
%doc README STACK TASKS
%doc doc notes tools subversion/LICENSE
/usr/lib/*
/usr/bin/*

