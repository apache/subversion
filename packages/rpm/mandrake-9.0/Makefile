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

SVN_TOP_DIR := $(shell pwd)/../../..

SPECFILE := subversion.spec
VERSION_FILE := $(SVN_TOP_DIR)/subversion/include/svn_version.h

RPMDIR := rpm

NAME := subversion
SVN_VERSION_MAJOR := $(shell grep 'define SVN_VER_MAJOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MINOR := $(shell grep 'define SVN_VER_MINOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MICRO := $(shell grep 'define SVN_VER_MICRO' < $(VERSION_FILE) | (read one two three; echo $$three) )
VERSION := $(SVN_VERSION_MAJOR).$(SVN_VERSION_MINOR).$(SVN_VERSION_MICRO)

RELEASE := $(shell svn st -v $(SPECFILE) | cut -b 10- | ( read one two ; echo $$one ) )
MDK_RELEASE := 1

SPECS_RPM_DIR := $(HOME)/$(RPMDIR)/SPECS
SOURCE_RPM_DIR := $(HOME)/$(RPMDIR)/SOURCES
SOURCES := cvs2svn.patch \
           python_swig_setup.py.patch \
	   svn-install.patch \
	   46_mod_dav_svn.conf \
           rcsparse.py

TARBALL := $(SOURCE_RPM_DIR)/$(NAME)-$(VERSION)-$(RELEASE).tar.bz2
SRC_DIR := $(TMPDIR)/$(NAME)-$(VERSION)

RPMBITS := $(HOME)/$(RPMDIR) $(HOME)/.rpmmacros $(HOME)/.rpmrc

.PHONY: all subversion rpm check_release build_rpm_files build_sources

all : rpm

subversion rpm : check_release build_sources build_rpm_files

check_release : $(RPMBITS)
	@echo "Making $(NAME)-$(VERSION)-$(RELEASE).$(MDK_RELEASE)mdk (S)RPM..."

build_rpm_files : build_sources
	cd $(SPECS_RPM_DIR); rpm -ba $(SPECFILE)

build_sources: $(TARBALL) $(RPMBITS) subversion.spec
	cp -f $(SOURCES) $(SOURCE_RPM_DIR)
	sed -e 's/@MDK_RELEASE@/$(MDK_RELEASE)/' \
		-e 's/@REPOS_REV@/$(RELEASE)/' \
		-e 's/@VERSION@/$(VERSION)/' \
		< $(SPECFILE) > $(SPECS_RPM_DIR)/$(SPECFILE)
	rm -f $(SRC_DIR)

build_tarball: $(TARBALL)

$(TARBALL): $(SRC_DIR)
	( cd $(SRC_DIR)/.. && \
	  tar chj --exclude .svn -f $(TARBALL) $(NAME)-$(VERSION) )
	rm -f $(SRC_DIR)

$(SRC_DIR):
	ln -sf $(SVN_TOP_DIR) $(SRC_DIR)

$(RPMBITS):
	@echo "Please follow the RPM builders howto at:"
	@echo
	@echo "http://www.linux-mandrake.com/howtos/mdk-rpm/"
	@echo
	@echo "before trying to build these RPMs."
	@exit 1

