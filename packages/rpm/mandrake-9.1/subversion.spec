# if you see @VERSION@ or @RELEASE@ here, then this spec file
# was not generated from the makefile. you can replace these tokens
# with the appropraite numbers, or use the makefile.

# To provide protection against accidental ~/.rpmmacros override
%define _topdir        @RPMDIR@
%define _builddir      %{_topdir}/BUILD
%define _rpmdir        %{_topdir}/RPMS
%define _sourcedir     %{_topdir}/SOURCES
%define _specdir       %{_topdir}/SPECS
%define _srcrpmdir     %{_topdir}/SRPMS

%define _tmppath /tmp
%define _signature	gpg
%define _gpg_name	Mandrake Linux
%define _gpg_path	./gnupg
%define distribution	Mandrake Linux
%define vendor	MandrakeSoft

%define apache_ver   @APACHE_VER@
%define apache_dir   @APACHE_DIR@
%define apache_conf  @APACHE_CONF@
%define db4_rpm      @DB4_RPM@
%define db4_ver      @DB4_VER@
%define neon_rpm     @NEON_RPM@
%define neon_ver     @NEON_VER@
%define mod_activate @MOD_ACTIVATE@
%define svn_root     @BUILDROOT@
%define namever      @NAMEVER@
%define relver       @RELVER@

@BLESSED@
@RELEASE_MODE@
@SKIP_DEPS@
@APR_CONFIG@
@APU_CONFIG@
@APXS@
@WITH_APXS@
@WITH_APR@
@WITH_APU@
@EDITOR@
@SILENT@
@SILENT_FLAG@
@USE_APACHE2@

%define name subversion
%define version      @VERSION@
%define release      @RELVER@mdk
%define repos_rev    @REPOS_REV@
%define usr /usr
%define build_dir   $RPM_BUILD_DIR/%{name}-%{version}
%define lib_dir     %{build_dir}/%{name}
%define tarball     %{name}-%{namever}.tar.bz2
%define mod_conf    46_mod_dav_svn.conf
%define rc_file     subversion.rc-%{namever}
%define py_patch    python_swig_setup.py.patch
%define svn_patch   svn-install.patch-%{namever}
%define svn_version svn-version.patch-%{namever}

Summary:	Wicked CVS Replacement
Name:		%{name}
Version:	%{version}
Release:	%{release}
License:	BSD
URL:		http://subversion.tigris.org
Source0:	%{tarball}
Source1:	%{mod_conf}
Source2:	%{rc_file}
Source3:	%{py_patch}
Patch0:		%{svn_patch}
Patch1:		%{svn_version}
Packager:	Michael Ballbach <ballbach@rten.net>
BuildRoot:      %{svn_root}
BuildRequires:	apache2-devel >= %{apache_ver}
BuildRequires:	%{neon_rpm}-devel >= %{neon_ver}
BuildRequires:	%{db4_rpm}-devel >= %{db4_ver}
BuildRequires:	texinfo
BuildRequires:	zlib-devel
BuildRequires:	autoconf2.5 >= 2.50
BuildRequires:	bison
BuildRequires:	flex
BuildRequires:	libldap2-devel
BuildRequires:	libsasl2-devel
BuildRequires:  krb5-devel
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
Requires: libapr0 >= %{apache_ver}
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
Requires: %{db4_rpm} >= %{db4_ver}
%description repos
This package contains the libraries required to allow subversion to access
local repositories. In addition, subversion-client-local is required for the
subversion client (`svn') to utilize these repositories directly.

This package also includes the `svnadmin' and `svnlook' programs.

%if %{use_apache2}
%package server
Summary:  Subversion Server Module for Apache
Group:    Development/Other
Requires: apache2-mod_dav >= %{apache-ver}
Requires: %{name}-repos = %{version}-%{release}
%description server
The apache2 server extension SO for running a subversion server.

%endif
%package client-dav
Summary:  Network Web-DAV Repository Access for the Subversion Client
Group:    Development/Other
Requires: %{name}-client-common = %{version}-%{release}
Requires: %{neon_rpm} >= %{neon_ver}
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
Requires: db4-utils => %{db4_ver}
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

%if %{use_apache2}
%files server
%defattr(-,root,root)
%config %{apache_conf}/%{mod_conf}
%{apache_dir}/mod_authz_svn.so
%{apache_dir}/mod_dav_svn.so
%endif

################################
######### Build Stages ######### 
################################
%prep
%setup -q -n %{name}-%{namever}
%patch0 -p1
%patch1 -p1
./autogen.sh %{?release_mode} \
	     %{?skip_deps}
LDFLAGS="-L%{lib_dir}/libsvn_client/.libs \
	-L%{lib_dir}/libsvn_delta/.libs \
	-L%{lib_dir}/libsvn_fs/.libs \
	-L%{lib_dir}/libsvn_repos/.libs \
	-L%{lib_dir}/libsvn_ra/.libs \
	-L%{lib_dir}/libsvn_ra_dav/.libs \
	-L%{lib_dir}/libsvn_ra_local/.libs \
	-L%{lib_dir}/libsvn_subr/.libs \
	-L%{lib_dir}/libsvn_wc/.libs" \
	./configure \
	%{?silent} \
	%{mod_activate} \
	--prefix=%{usr} \
	--mandir=%{usr}/share/man \
	--libexecdir=%{apache_dir} \
	--sysconfdir=%{apache_conf} \
	--with-swig \
	--enable-shared \
	--enable-dso \
	--with-berkeley-db=%{usr} \
	%{?editor} \
	%{?with_apr}%{?apr_config} \
	%{?with_apu}%{?apu_config} \
	--with-neon=%{usr} \
	%{?with_apxs}%{?apxs}
%build
DESTDIR="$RPM_BUILD_ROOT" %make %{?silent_flag}

################################
######### Installation ######### 
################################
%install
rm -rf "$RPM_BUILD_ROOT"

# do the normal make install, and copy our apache2 configuration file
DESTDIR="$RPM_BUILD_ROOT" \
	prefix=%{usr} \
	mandir=%{usr}/share/man \
	base_libdir=%{usr}/lib \
	libexecdir=%{apache_dir} \
	sysconfdir=%{apache_conf} \
	fs_libdir=%{usr}/lib \
	fs_bindir=%{usr}/bin \
	swig_py_libdir=%{usr}/lib \
	make %{?silent_flag} install 
	
%if %{use_apache2}
mkdir -p $RPM_BUILD_ROOT/%{apache_conf}
cp %{SOURCE1} $RPM_BUILD_ROOT/%{apache_conf}
%endif

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

%pre server
APACHECTL=/usr/sbin/apachectl
if [ -x "$APACHECTL" ] && [ "$USER" == "root" ] ; then
  $APACHECTL stop
else
  echo Unable to stop apache - need to be root
fi
%post server 
APACHECTL=/usr/sbin/apachectl
if [ -x "$APACHECTL" ] && [ "$USER" == "root" ] ; then
  if $APACHECTL configtest ; then
    $APACHECTL start
  else
    echo Please check your apache configuration
  fi
else
  echo Unable to stop apache - need to be root
fi
%preun server
APACHECTL=/usr/sbin/apachectl
if [ -x "$APACHECTL" ] && [ "$USER" == "root" ] ; then
  $APACHECTL stop
fi
%postun server
APACHECTL=/usr/sbin/apachectl
if [ -x "$APACHECTL" ] && [ "$USER" == "root" ] ; then
  if $APACHECTL configtest ; then
    $APACHECTL start
  else
    echo Please check your apache configuration
  fi
else
  echo Unable to stop apache - need to be root
fi

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

