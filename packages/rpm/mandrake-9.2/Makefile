# this makefile will build the tarball and setup the links in
# your ~/rpm directory to build the mandrake RPMs. This makefile
# is based on the redhat one - but it is a bit different. it wont
# create an rpm directory structure for you, nor will it create the
# .rpmmacros etc.. files. Set these up based on the RPM howto
# that mandrake publishes:
#
# http://www.linux-mandrake.com/howtos/mdk-rpm/
#
# You should try the `check_release' target before you make all to make
# sure all the version numbers were groked correctly.
#
# This makefile doesn't copy the source tree to make a tarball, it 
# will create a symlink in your $TMPDIR and tar up your source tree
# in place. This makefile uses the output tarball name as a dependancy,
# so it wont constantly regenerate it unless your repository version
# number changes.
#
# you may append a definition for MDK_RELEASE on your make line to
# change the number that comes before mdk. i.e., make MDK_RELEASE=2
# will make a ...3410.2mdk or similair.

####################################################################
#                        CONFIGURATION AREA                        #
####################################################################
# Signature elements
# GPG_PATH := ~/.gnupg

# Where our temp directory is
# This is where the compiled results will go prior to installation
# This is also where the build tree will go if you are not root
# TMPDIR :=/tmp

# Allow --nodeps flag 0/1
# Setting this to 1 ignores rpm dependencies during builds
# NODEPS := 1

# Docs only please
# DOCS_ONLY := 1 

# No documentation please
# NO_DOCS := 1

# Silent 0/1
# Setting this to 1 removes white noise
# SILENT := 0

# No module activation - conf.d files control modules 0/1
# Setting this to 0 puts the modules in to httpd2.conf - ugh
# NO_ACTIVATION := 1

# No APACHE
# Set this to one if you want to build w/o Apache
# NO_APACHE2 := 0

# Require Apache? 0/1
# Setting this to 1 causes an abort if apache2 is not found
# REQUIRE_APACHE2 := 1

# Official Build
# Setting this causes the system to use the build revision
# This can only be set to sanctioned build numbers
# BLESSED := 6982

# FOP location
# FOP := /opt/fop

# FOP options
# FOP_OPTS := -q


# The specific release number
MDK_RELEASE := 1

# Release mode
# Default is 0
# WIN32_RELEASE := 1

# swig rpm name - change this to suit your needs
# Mandrake calls this swig, tigris calls this swig-runtime
# SWIG := swig-runtime

# swig-devel rpm name - change this to suit your needs
# Mandrake calls this swig, tigris calls this swig
# SWIG_BUILD := swig

# libneon0 rpm name - change this to suit your needs
# Currently libneon's rpms seem to be called neon on tigris
# but are called libneon0 on Mandrake
# NEON := neon

# Default editor
# Change this to something else if you like
# This is the most basic
# Comment this to not define an editor
EDITOR := vi

# Override Apache conf - comment if not needed
# Default is `apxs2 -q sysconfdir`.d
APACHE_CONF := /etc/httpd/conf.d

# Override Apache directory  - comment if not needed
# Default is `apxs2 -q libexecdir`-extramodules
#APACHE_DIR := /usr/lib/apache2-extramodules

# Minimal versions
# NEON_MIN   := 0.23.0
# SWIG_MIN   := 1.3.16
# APACHE_MIN := 2.0.43
# DB4_MIN    := 4.0.14

####################################################################
#                    END OF CONFIGURATION AREA                     #
####################################################################

# Detect user
ifeq ($(USER),root)
RPMDIR := /usr/src/RPM
else
RPMDIR := $(TMPDIR)/$(USER)-rpm
endif

# Directories used
SOURCE_RPM_DIR := $(RPMDIR)/SOURCES
SPECS_RPM_DIR  := $(RPMDIR)/SPECS
BUILD_RPM_DIR  := $(RPMDIR)/BUILD
i586_RPM_DIR   := $(RPMDIR)/RPMS/i586
SRPMS_RPM_DIR  := $(RPMDIR)/SRPMS

# Where the svn executable is
SVNEXEC := $(shell which svn 2>/dev/null)

# Whether this is being compiled on the trunk
TRUNK := $(if $(SVNEXEC),$(shell $(SVNEXEC) info . | grep Url),$(shell pwd))
TRUNK := $(shell echo $(TRUNK) | grep trunk | wc -l | sed 's/ //g')

ifneq ($(TRUNK),0)
TRUNK := 1
PRE   := pre
else
TRUNK :=
PRE   :=
endif

# Files and locations
# Our top level directory
SVN_TOP_DIR := $(shell pwd)/../../..

# Where our version is
VERSION_FILE := $(SVN_TOP_DIR)/subversion/include/svn_version.h

# Our specific spec file
SPECFILE := subversion.spec

# Custom RC file
RCFILE   := subversion.rc

# Basic definitions
NAME := subversion
SVN_VERSION_MAJOR := $(shell grep 'define SVN_VER_MAJOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MINOR := $(shell grep 'define SVN_VER_MINOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MINOR := $(if $(TRUNK),$(shell sh increment-revision $(SVN_VERSION_MINOR)),$(SVN_VERSION_MINOR))
SVN_VERSION_MICRO := $(shell grep 'define SVN_VER_MICRO' < $(VERSION_FILE) | (read one two three; echo $$three) )
VERSION := $(PRE)$(SVN_VERSION_MAJOR).$(SVN_VERSION_MINOR).$(SVN_VERSION_MICRO)

# The relase depends on whether this is a blessed copy or not
# If this is a blessed copy, the release is what's defined
# If this is not a blessed copy, the release is what's found
# If subversion is not installed, RELEASE can be specified on
# the command line
ifneq ($(strip $(BLESSED)),)
RELEASE := $(BLESSED)
else
RELEASE := $(if $(SVNEXEC),$(shell $(SVNEXEC) st -v $(SPECFILE) | cut -b 10- | ( read one two ; echo $$one ) ),)
endif

# Default values
GE_30           := $(shell sh ge-0-30-0 $(SVN_VERSION_MAJOR) $(SVN_VERSION_MINOR) $(SVN_VERSION_MICRO))
GE_30_EXT       := $(if $(GE_30),-ge-0-30-0,)
TMPDIR          := $(if $(TMPDIR),$(TMPDIR),/tmp)
NODEPS          := $(if $(NODEPS),$(NODEPS),0)
SILENT          := $(if $(SILENT),$(SILENT),1)
DOCS_ONLY       := $(if $(DOCS_ONLY),$(DOCS_ONLY),0)
NO_DOCS         := $(if $(NO_DOCS),$(NO_DOCS),0)
NO_ACTIVATION   := $(if $(NO_ACTIVATION),$(NO_ACTIVATION),1)
NO_APACHE2      := $(if $(NO_APACHE2),$(NO_APACHE2),0)
REQUIRE_APACHE2 := $(if $(REQUIRE_APACHE2),$(REQUIRE_APACHE2),1)
NEON_RPM        := $(if $(NEON),$(NEON),libneon0)
SWIG_RPM        := $(if $(SWIG),$(SWIG),swig)
SWIG_BUILD_RPM  := $(if $(SWIG_BUILD),$(SWIG_BUILD),swig)
SWIG_MIN        := $(if $(SWIG_MIN),$(SWIG_MIN),$(if $(GE_30),1.3.16,1.3.16))
NEON_MIN        := $(if $(NEON_MIN),$(NEON_MIN),$(if $(GE_30),0.24.0,0.23.0))
APACHE_MIN      := $(if $(APACHE_MIN),$(APACHE_MIN),$(if $(GE_30),2.0.47,2.0.43))
DB4_MIN         := $(if $(DB4_MIN),$(DB4_MIN),$(if $(GE_30),4.1.25,4.0.14))
GPG_PATH        := $(if $(GPG_PATH),$(GPG_PATH),\~/.gnupg)
GPG_PATH        := $(shell echo $(GPG_PATH)|sed 's/\//\\\//g' | sed 's/\./\\\./g')
WIN32_RELEASE   := $(if $(WIN32_RELEASE),$(WIN32_RELEASE),0)
MDK_RELEASE     := $(if $(MDK_RELEASE),$(MDK_RELEASE),1)
DOCBOOK         := $(shell rpm -q docbook-style-xsl)
XSLT            := $(shell rpm -q libxslt-proc)
FOP_PGM         := $(shell sh fop-installed $(FOP))
FOP_OPTS := $(if ($FOP_OPTS),\%define fop_opts $(FOP_OPTS),\%undefine fop_opts)
ifeq ($(strip $(FOP_PGM)),0)
FOP_SOURCE    := $(shell sh fop-file)
FOP_BZ2       := $(shell sh fop-bz2 $(FOP_SOURCE))
FOP_GZ        := $(shell sh fop-gz $(FOP_SOURCE))
FOP           := $(strip $(FOP))
FOP           := $(if $(FOP),$(FOP),$(if $(FOP_SOURCE),$(if $(FOP_BZ2),$(FOP_BZ2),$(if $(FOP_GZ),$(FOP_GZ),))))
ifneq ($(strip $(FOP)),)
FOP_ARC := $(if $(FOP_BZ2),j,$(if $(FOP_GZ),z,))
FOP_DIR := \%define fop_dir $(shell tar $(FOP_ARC)tvf $(FOP_SOURCE) | head -1 | cut -f3 -d: | cut -f2 -d\  | cut -f1 -d/)
FOP_ARC := $(if $(FOP_ARC),\%define fop_arc tar $(FOP_ARC)xf $(SOURCE_RPM_DIR)/$(FOP_SOURCE),\%undefine fop_arc)
FOP_ARC := $(shell echo $(FOP_ARC)| sed 's/\//\\\//g')
FOP_SRC := \%define fop_src $(FOP_SOURCE)
USE_FOP := 1
else
FOP_ARC := \%undefine fop_arc
FOP_SRC := \%undefine fop_src
FOP_DIR  := \%undefine fop_dir
USE_FOP := 0
endif
else
FOP_ARC := \%undefine fop_arc
FOP_SRC := \%undefine fop_src
FOP_DIR  := \%undefine fop_dir
USE_FOP := 1
endif
ifneq ($(strip $(XSLT)),)
ifneq ($(strip $(DOCBOOK)),)
ifneq ($(strip $(USE_FOP)),0)
DOCBOOK_PATH:=$(shell rpm -q -l docbook-style-xsl | grep stylesheets$$)
endif
endif
endif

ifeq ($(strip $(DOCBOOK_PATH)),)
NO_DOCS := 1
endif


ifeq ($(NO_DOCS),1)
DOCBOOK_PATH :=
NOT_JUST_DOCS:= \%define not_just_docs 1
MAKE_DOC     := \%undefine docbook
FOP_PATH     := \%undefine fop
else
ORG_DOCBOOK := $(DOCBOOK_PATH)
DOCBOOK_PATH :=$(shell echo $(DOCBOOK_PATH)| sed 's/\//\\\//g')
MAKE_DOC:=$(if $(DOCBOOK_PATH),\%define docbook $(DOCBOOK_PATH),\%undefine docbook)
NOT_JUST_DOCS := \%define not_just_docs $(if $(shell sh just-docs $(DOCS_ONLY)),0,1)
FOP_PATH      := $(if $(FOP),\%define fop $(FOP),\%undefine fop)
endif


ifeq ($(WIN32_RELEASE),1)
RELEASE_MODE    := \%define release_mode --release
else
RELEASE_MODE    := \%undefine release_mode
endif
ifeq ($(NODEPS),1)
SKIP_DEPS       := \%define skip_deps -s
else
SKIP_DEPS       := \%undefine skip_deps
endif

# Predefined complete names
ifeq ($(strip $(BLESSED)),)
BLESSDEF:= \%undefine blessed
BLESS   :=
NAMEVER  := $(VERSION)-$(RELEASE)
RELVER   := $(RELEASE).$(MDK_RELEASE)
FULLVER  := $(NAMEVER).$(MDK_RELEASE)
else
BLESS   := 1
BLESSDEF:= \%define blessed $(BLESSED)
NAMEVER  := $(VERSION)
RELVER   := $(MDK_RELEASE)
FULLVER  := $(NAMEVER)-$(MDK_RELEASE)
endif
BASENAME := $(NAME)-$(NAMEVER)
SHORTNAME := $(NAME)-$(FULLVER)

# Translate dependencies
ifeq ($(NODEPS),1)
NODEPS := --nodeps
else
NODEPS :=
endif
ifeq ($(SILENT),1)
SILENT      := \%define silent --silent
SILENT_FLAG := \%define silent_flag -s
# Trunk changes the silent patch slightly
PATCH       := svn-install.patch svn-book.patch svn-book-fop$(GE_30_EXT).patch svn-install-silent$(GE_30_EXT).patch
SILENT_TGT  := all_silent
else
SILENT      := \%undefine silent
SILENT_FLAG :=  \%undefine silent_flag
PATCH       := svn-install.patch svn-book.patch svn-book-fop$(GE_30_EXT).patch
SILENT_TGT  := all_noisy
endif
ifeq ($(NO_ACTIVATION),1)
MOD_ACTIVATE := --disable-mod-activation
else
MOD_ACTIVATE := --enable-mod-activation
endif

# Assemble all rc files
DEFRC1   := $(shell ls /usr/lib/rpm/rpmrc 2>/dev/null | grep rpm)
DEFRC2   := $(shell ls /etc/rpmrc 2>/dev/null | grep rpm)
DEFRC3   := $(shell ls ~/.rpmrc 2>/dev/null | grep rpm)
RCFILES  := $(if $(DEFRC1),$(DEFRC1):)$(if $(DEFRC2),$(DEFRC2):)$(if $(DEFRC3),$(DEFRC3):)$(SOURCE_RPM_DIR)/$(RCFILE)-$(NAMEVER)

# Define programs
ifneq ($(NO_APACHE2),1)
APXS          := $(shell which apxs2|sed 's/\//\\\//g')
APR_CONFIG    := $(shell which apr-config| sed 's/\//\\\//g')
APU_CONFIG    := $(shell which apu-config| sed 's/\//\\\//g')
WITH_APR      := \%define with_apr $(if $(APR_CONFIG),--with-apr-config=,--without-apr-config)
WITH_APU      := $(if $(APU_CONFIG),\%define with_apu --with-apr-util=,)
USE_APACHE2      := \%define use_apache2 1
else
USE_APACHE2      := \%define use_apache2 0
REQUIRE_APACHE2 := 0
endif
ifneq ($(strip $(APXS)),)
WITH_APXS     := \%define with_apxs --with-apxs=
APACHE_CONF   := $(if $(APACHE_CONF),$(APACHE_CONF),$(shell $(APXS) -q sysconfdir).d)
APACHE_DIR    := $(if $(APACHE_DIR),$(APACHE_DIR),$(shell $(APXS) -q libexecdir)-extramodules)
else
WITH_APXS     := \%define with_apxs --without-apxs
APACHE_CONF   := $(if $(APACHE_CONF),$(APACHE_CONF),/etc/httpd/conf.d)
APACHE_DIR    := $(if $(APACHE_DIR),$(APACHE_DIR),/usr/lib/apache2-extramodules)
endif
APXS       := $(if $(APXS),\%define apxs $(APXS),\%undefine apxs)
APR_CONFIG := $(if $(APR_CONFIG),\%define apr_config $(APR_CONFIG),\%undefine apr_config)
APU_CONFIG := $(if $(APU_CONFIG),\%define apu_config $(APU_CONFIG),\%undefine apu_config)

# Define versions
ifneq ($(NO_APACHE2),1)
APACHE2  := $(shell rpm -q -l apache2 | grep sbin | grep httpd2 )
else
NO_APACHE2 := 0
endif
ifeq ($(strip $(APACHE2)),)
ifeq ($(REQUIRE_APACHE2),1)
QUIT := quit
endif
DB4_VER := $(DB4_MIN)
DB4_RPM := libdb$(shell echo $(DB4_MIN) | cut -f1,2 -d\.)
else
DB4      := $(shell ldd $(APACHE2) | grep libdb | head -1 | cut -f2,3,4 -d/ | cut -f1 "-d ")
DB4_VER  := $(if $(DB4),$(shell rpm -q -f /$(DB4) | cut -f2 -d- ),4.0.14)
DB4_RPM  := $(if $(DB4),$(shell rpm -q -f /$(DB4) | cut -f1 -d-),libdb4.0)
endif

# Where the user will build
BUILDROOT := /tmp/$(USER)-$(BASENAME)-root

# Sed usable versions of macros
SEDRPM := $(shell echo $(RPMDIR) | sed 's/\//\\\//g')
SEDTMP := $(shell echo $(TMPDIR) | sed 's/\//\\\//g')
SEDROOT := $(shell echo $(BUILDROOT) | sed 's/\//\\\//g')
APACHE_DIR := $(shell echo $(APACHE_DIR) | sed 's/\//\\\//g')
APACHE_CONF := $(shell echo $(APACHE_CONF) | sed 's/\//\\\//g')
EDITOR      := $(if $(EDITOR),\%define editor --with-editor=$(shell which $(EDITOR) | sed 's/\//\\\//g'),)

SOURCES := 46_mod_dav_svn.conf $(FOP_SOURCE)

# Replace periods with dashes
TARBALL := $(SOURCE_RPM_DIR)/$(BASENAME).tar.bz2
SRC_DIR := $(TMPDIR)/$(BASENAME)

RPMBITS := $(SOURCE_RPM_DIR) \
	   $(SPECS_RPM_DIR) \
	   $(BUILD_RPM_DIR) \
	   $(SRPMS_RPM_DIR) \
	   $(i586_RPM_DIR)

.PHONY: all all_silent all_noisy subversion rpm check_release build_rpm_files build_sources quit

all : $(SILENT_TGT)

all_noisy : $(QUIT) rpm

all_silent :
	@make -e -s all_noisy

subversion rpm : check_release build_sources build_rpm_files

check_release : $(RPMBITS)
	@if [ "$(RELEASE)"x = "x" ] ; then \
	  echo "Subversion not installed or this is not a blessed version." ; \
	  echo "Try 'make RELEASE=XXXX' instead"; exit 1 ; \
	else  \
	  exit 0 ;  \
	fi
	@echo "Making $(SHORTNAME)mdk (S)RPM..."

build_rpm_files : build_sources $(BUILDROOT)
	@echo Building RPM
	cd $(SPECS_RPM_DIR); rpm --rmspec --clean --rcfile $(RCFILES) -ba $(SPECFILE)-$(NAMEVER) $(NODEPS)

build_sources: $(TARBALL) $(RPMBITS) 
	@echo Preparing sources and specfile for RPM
	cp -f $(SOURCES) $(SOURCE_RPM_DIR)
	cp -f $(RCFILE) $(SOURCE_RPM_DIR)/$(RCFILE)-$(NAMEVER)
	cat $(PATCH) > $(SOURCE_RPM_DIR)/svn-install.patch-$(NAMEVER)
	if [ "$(DOCBOOK_PATH)" != "" ] ; then sh make-fo-patch $(ORG_DOCBOOK) >> $(SOURCE_RPM_DIR)/svn-install.patch-$(NAMEVER) ; fi
	sh make-version-patch $(VERSION_FILE) $(RELEASE) $(MDK_RELEASE) $(BLESS) > $(SOURCE_RPM_DIR)/svn-version.patch-$(NAMEVER)
	sed -e 's/@MDK_RELEASE@/$(MDK_RELEASE)/' \
		-e 's/@REPOS_REV@/$(RELEASE)/' \
		-e 's/@NAMEVER@/$(NAMEVER)/' \
		-e 's/@RELVER@/$(RELVER)/' \
		-e 's/@BLESSED@/$(BLESSDEF)/' \
		-e 's/@VERSION@/$(VERSION)/' \
		-e 's/@RPMDIR@/$(SEDRPM)/' \
		-e 's/@FOP_ARC@/$(FOP_ARC)/' \
		-e 's/@FOP_OPTS@/$(FOP_OPTS)/' \
		-e 's/@FOP_DIR@/$(FOP_DIR)/' \
		-e 's/@FOP_SRC@/$(FOP_SRC)/' \
		-e 's/@TMPDIR@/$(SEDTMP)/' \
		-e 's/@BUILDROOT@/$(SEDROOT)/' \
		-e 's/@DB4_RPM@/$(DB4_RPM)/' \
		-e 's/@DB4_VER@/$(DB4_VER)/' \
		-e 's/@APXS@/$(APXS)/' \
		-e 's/@MAKE_DOC@/$(MAKE_DOC)/' \
		-e 's/@FOP_PATH@/$(FOP_PATH)/' \
		-e 's/@NOT_JUST_DOCS@/$(NOT_JUST_DOCS)/' \
		-e 's/@GPG_PATH@/$(GPG_PATH)/' \
		-e 's/@APR_CONFIG@/$(APR_CONFIG)/' \
		-e 's/@APU_CONFIG@/$(APU_CONFIG)/' \
		-e 's/@WITH_APXS@/$(WITH_APXS)/' \
		-e 's/@WITH_APR@/$(WITH_APR)/' \
		-e 's/@WITH_APU@/$(WITH_APU)/' \
		-e 's/@APACHE_DIR@/$(APACHE_DIR)/' \
		-e 's/@USE_APACHE2@/$(USE_APACHE2)/' \
		-e 's/@APACHE_CONF@/$(APACHE_CONF)/' \
		-e 's/@APACHE_VER@/$(APACHE_MIN)/' \
		-e 's/@NEON_RPM@/$(NEON_RPM)/' \
		-e 's/@NEON_VER@/$(NEON_MIN)/' \
		-e 's/@SWIG_VER@/$(SWIG_MIN)/' \
		-e 's/@SWIG_RPM@/$(SWIG_RPM)/' \
		-e 's/@SWIG_BUILD_RPM@/$(SWIG_BUILD_RPM)/' \
		-e 's/@SILENT@/$(SILENT)/' \
		-e 's/@EDITOR@/$(EDITOR)/' \
		-e 's/@RELEASE_MODE@/$(RELEASE_MODE)/' \
		-e 's/@SKIP_DEPS@/$(SKIP_DEPS)/' \
		-e 's/@SILENT_FLAG@/$(SILENT_FLAG)/' \
		-e 's/@MOD_ACTIVATE@/$(MOD_ACTIVATE)/' \
		< $(SPECFILE) > $(SPECS_RPM_DIR)/$(SPECFILE)-$(NAMEVER)
	rm -f $(SRC_DIR)

build_tarball: $(TARBALL)

$(TARBALL): $(SRC_DIR)
	@echo Creating tarball
	( cd $(SRC_DIR)/.. && \
	  tar chj --exclude .svn -f $(TARBALL) $(NAME)-$(NAMEVER) )
	rm -f $(SRC_DIR)

$(SRC_DIR):
	@echo Creating symlink for tarball
	ln -sf $(SVN_TOP_DIR) $(SRC_DIR)

$(SOURCE_RPM_DIR):
	mkdir -p $(SOURCE_RPM_DIR)
$(SPECS_RPM_DIR):
	mkdir -p $(SPECS_RPM_DIR)
$(i586_RPM_DIR):
	mkdir -p $(i586_RPM_DIR)
$(SRPMS_RPM_DIR):
	mkdir -p $(SRPMS_RPM_DIR)
$(BUILD_RPM_DIR):
	mkdir -p $(BUILD_RPM_DIR)
$(BUILDROOT):
	mkdir -p $(BUILDROOT)
quit:
	@echo Apache 2.0.43 or better required to bootstrap subversion
	@echo Please try again after installing at least Apache 2.0.43
	@exit 1
