# if you see @VERSION@ or @RELEASE@ here, then this spec file
# was not generated from the makefile. you can replace these tokens
# with the appropraite numbers, or use the makefile.

%define name subversion
%define version @VERSION@
%define repos_rev @REPOS_REV@
%define release @REPOS_REV@.@MDK_RELEASE@mdk
%define apache_ver 2.0.43

Summary:	Wicked CVS Replacement
Name:		%{name}
Version:	%{version}
Release:	%{release}
License:	BSD
URL:		http://subversion.tigris.org
Source0:	subversion-%{version}-%{repos_rev}.tar.bz2
Source1:	46_mod_dav_svn.conf
Patch0:		svn-install.patch
Packager:	Michael Ballbach <ballbach@rten.net>
BuildRoot:	%_tmppath/%name-%version-%release-root
BuildRequires:	apache2-devel >= %{apache_ver}
BuildRequires:	libneon0.23-devel >= 0.23.4
BuildRequires:	libdb4.0-devel >= 4.0.14
BuildRequires:	texinfo
BuildRequires:	zlib-devel
BuildRequires:	autoconf2.5 >= 2.50
BuildRequires:	bison
BuildRequires:	flex
BuildRequires:	libldap2-devel
BuildRequires:	libsasl2-devel
BuildRequires:	python >= 2.2.0
BuildRequires:	libpython2.2-devel
BuildRequires:	swig
Group:		Development/Other

%description
Subversion is a concurrent version control system which enables one or more
users to collaborate in developing and maintaining a hierarchy of files and
directories while keeping a history of all changes.  Subversion only stores the
differences between versions, instead of every complete file.  Subversion also
keeps a log of who, when, and why changes occurred.

As such it basically does the same thing CVS does (Concurrent Versioning
System) but has major enhancements compared to CVS and fixes a lot of the
annoyances that CVS users face.

This package contains the common files required by subversion clients and
servers.

#####################################
###### Sub-Package Definitions ###### 
#####################################
%package base
Provides: %{name} = %{version}-%{release}
Group:    Development/Other
Summary:  Common Files for Subversion
Requires: libapr0 >= 2.0.43
%description base
This package contains all the common files required to run subversion
components.

%package client-common
Summary:  Common Client Libs for Subversion
Group:    Development/Other
Requires: %{name}-base = %{version}-%{release}
%description client-common
This package contains the common libraries required to run a subversion client.
You'll want to install the subversion-client-dav or subversion-client-local to
access network or local repositories, respectfully. There is also a package
called subversion-client-svn for remote svn client access. (NOT over WebDAV)

%package repos
Summary:  Local Repository Access for Subversion
Group:    Development/Other
Requires: %{name}-base = %{version}-%{release}
Requires: libdb4.0 >= 4.0.14
%description repos
This package contains the libraries required to allow subversion to access
local repositories. In addition, subversion-client-local is required for the
subversion client (`svn') to utilize these repositories directly.

This package also includes the `svnadmin' and `svnlook' programs.

%package server
Summary:  Subversion Server Module for Apache
Group:    Development/Other
Requires: apache2-mod_dav >= %{apache-ver}
Requires: %{name}-repos = %{version}-%{release}
%description server
The apache2 server extension SO for running a subversion server.

%package client-dav
Summary:  Network Web-DAV Repository Access for the Subversion Client
Group:    Development/Other
Requires: %{name}-client-common = %{version}-%{release}
Requires: libneon0.23 >= 0.23.4
%description client-dav
This package contains the libraries required to allow the subversion client
(`svn') to access network subversion repositories over Web-DAV.

%package client-svn
Summary:  Network Native Repository Access for the Subversion Client
Group:    Development/Other
Requires: %{name}-client-common = %{version}-%{release}
%description client-svn
This package contains the libraries required to allow the subversion client
(`svn') to access network subversion repositories natively.

%package client-local
Summary:  Local Repository Access for the Subversion Client
Group:    Development/Other
Requires: %{name}-client-common = %{version}-%{release}
Requires: %{name}-repos = %{version}-%{release}
%description client-local
This package contains the libraries required to allow the subversion client
(`svn') to access local subversion repositories.

%package devel
Summary:  Subversion Headers/Libraries for Development
Group:    Development/Other
Requires: %{name}-base = %{version}-%{release}
%description devel
This package contains the header files and linker scripts for subversion
libraries.

%package tools
Summary:  Subversion Misc. Tools
Group:    Development/Other
Requires: %{name}-base = %{version}-%{release}
Requires: db4-utils
%description tools
This package contains a myriad tools for subversion. This package also contains
'cvs2svn' - a program for migrating CVS repositories into Subversion repositories.
The package also contains all of the python bindings for the subersion API, 
required by several of the tools.

###########################
########## Files ########## 
###########################
%files base
%defattr(-,root,root)
%doc doc BUGS CHANGES COPYING HACKING IDEAS README
%{_libdir}/libsvn_delta-*so*
%{_libdir}/libsvn_fs-*so*
%{_libdir}/libsvn_subr-*so*
%{_libdir}/libsvn_wc-*so*
%{_libdir}/libsvn_diff-*so*
%{_mandir}/man1/svnadmin.*
%{_bindir}/svnversion
%{_bindir}/svnserve

%files repos
%defattr(-,root,root)
%{_libdir}/libsvn_repos-*so*
%{_bindir}/svnadmin
%{_bindir}/svndumpfilter
%{_bindir}/svnlook

%files client-common
%defattr(-,root,root)
%{_bindir}/svn
%{_mandir}/man1/svn.*
%{_libdir}/libsvn_client*so*
%{_libdir}/libsvn_ra-*so*

%files client-local
%defattr(-,root,root)
%{_libdir}/libsvn_ra_local-*so*

%files client-dav
%defattr(-,root,root)
%{_libdir}/libsvn_ra_dav-*so*

%files client-svn
%defattr(-,root,root)
%{_libdir}/libsvn_ra_svn-*so*

%files tools
%defattr(-,root,root)
%{_datadir}/%{name}-%{version}/tools
%{_libdir}/libsvn_swig_py*so*

%files devel
%defattr(-,root,root)
%{_libdir}/libsvn*.a
%{_libdir}/libsvn*.la
%{_includedir}/subversion-1

%files server
%defattr(-,root,root)
%config /etc/httpd/conf.d/46_mod_dav_svn.conf
/etc/httpd/2.0/modules/mod_authz_svn.la
/etc/httpd/2.0/modules/mod_authz_svn.so
/etc/httpd/2.0/modules/mod_dav_svn.la
/etc/httpd/2.0/modules/mod_dav_svn.so

################################
######### Build Stages ######### 
################################
%prep
%setup -q
./autogen.sh
LDFLAGS="-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_client/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_delta/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_fs/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_repos/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra_dav/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_ra_local/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_subr/.libs \
	-L$RPM_BUILD_DIR/subversion-%{version}/subversion/libsvn_wc/.libs" \
	./configure \
	--prefix=/usr \
	--with-swig \
	--enable-shared \
	--enable-dso \
	--with-berkeley-db=/usr \
	--with-apr=/usr/bin/apr-config \
	--with-apr-util=/usr/bin/apu-config \
	--with-neon=/usr \
	--with-apxs=/usr/sbin/apxs2
%patch0 -p1

%build
%make

################################
######### Installation ######### 
################################
%install
rm -rf $RPM_BUILD_ROOT

# do the normal make install, and copy our apache2 configuration file
make install \
	prefix=$RPM_BUILD_ROOT/usr \
	mandir=$RPM_BUILD_ROOT/usr/share/man \
	fs_libdir=$RPM_BUILD_ROOT/usr/lib \
	base_libdir=$RPM_BUILD_ROOT/usr/lib \
	libexecdir=$RPM_BUILD_ROOT/%{apache_dir}/lib
	fs_libdir=$RPM_BUILD_ROOT/usr/lib \
	fs_bindir=$RPM_BUILD_ROOT/usr/bin \
	base_libdir=$RPM_BUILD_ROOT/usr/lib \
	swig_py_libdir=$RPM_BUILD_ROOT/usr/lib
	
mkdir -p $RPM_BUILD_ROOT/etc/httpd/conf.d
cp %{SOURCE1} $RPM_BUILD_ROOT/etc/httpd/conf.d

# copy everything in tools into a share directory
mkdir -p $RPM_BUILD_ROOT/%{_datadir}/%{name}-%{version}
cp -r tools $RPM_BUILD_ROOT/%{_datadir}/%{name}-%{version}

%clean
rm -rf $RPM_BUILD_ROOT

##################################
###### Post and Pre Scripts ###### 
##################################
%post base -p /sbin/ldconfig
%postun base -p /sbin/ldconfig

%post devel -p /sbin/ldconfig
%postun devel -p /sbin/ldconfig

%post client-common -p /sbin/ldconfig
%postun client-common -p /sbin/ldconfig

%post client-local -p /sbin/ldconfig
%postun client-local -p /sbin/ldconfig

%post client-dav -p /sbin/ldconfig
%postun client-dav -p /sbin/ldconfig

%post client-svn -p /sbin/ldconfig
%postun client-svn -p /sbin/ldconfig

%post repos -p /sbin/ldconfig
%postun repos -p /sbin/ldconfig

############################
######## Change Log ######## 
############################
%changelog
* Mon Jun 30 2003 Michael Ballbach <ballbach@rten.net> 0.24.2-6372.2mdk
- cleaned up the spec file of the old python swig bindings stuff, rolled
  it all into the tools.

* Mon Jun 30 2003 Magnus Kessler <Magnus.Kessler@gmx.net> 0.24.2-6372.1mdk
- remove info stuff no longer being generated

* Mon Jun  2 2003 Magnus Kessler <Magnus.Kessler@gmx.net> 0.24.0-6109.1mdk
- updates for latest subversion

* Wed Apr  2 2003 Michael Ballbach <ballbach@rten.net> 0.20.1-5467.1mdk
- initial spec file for mandrake 9.1, needs a lot of work, will have
  updates in one or two days

* Sun Oct 20 2002 Michael Ballbach <ballbach@rten.net> 0.14.3-3421.1mdk
- added some more ldconfig's and some install-info like the redhat spec
  file does.

* Fri Oct 18 2002 Michael Ballbach <ballbach@rten.net> 0.14.3-3399.1mdk
- updated to 3399, and changed how I do the version numbers.
  I want the repository version in there, but we certainly need the
  'program' version number in there too. So, this accomidates RPMs
  during development and post development.

* Tue Oct 15 2002 Michael Ballbach <ballbach@rten.net> 3371-3mdk
- 3372, fixed a stupid bug due to my old file layout in cvs2svn

* Tue Oct 15 2002 Michael Ballbach <ballbach@rten.net> 3371-2mdk
- 3371, adopted the package dependancy scheme suggested by
  Greg Stein <gstein@lyra.org>

* Tue Oct 15 2002 Michael Ballbach <ballbach@rten.net> 3371-1mdk
- 3371, upgraded to svn current

* Thu Oct 10 2002 Michael Ballbach <ballbach@rten.net> 3347-1mdk
- 3347, initial

