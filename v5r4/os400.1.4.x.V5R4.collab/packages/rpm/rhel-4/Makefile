##########################################################################
# Build an RPM from the current spec file and subversion directory tree. #
#                                                                        #
# Note: The RPM and Makefile are set up to GENERATE RPMS as a non-root   #
# user in the user's home directory.  Of course to INSTALL the resulting #
# binary RPM the user has to log in as root.                             #
##########################################################################

SVN_TOP_DIR := $(shell pwd)/../../..

SPECFILE := subversion.spec
VERSION_FILE := $(SVN_TOP_DIR)/subversion/include/svn_version.h

RPMDIR := rpms

NAME := $(shell grep '^Name:' < $(SPECFILE) | sed -e 's/^Name: //')
SVN_VERSION_MAJOR := $(shell grep 'define SVN_VER_MAJOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MINOR := $(shell grep 'define SVN_VER_MINOR' < $(VERSION_FILE) | (read one two three; echo $$three) )
SVN_VERSION_MICRO := $(shell grep 'define SVN_VER_PATCH' < $(VERSION_FILE) | (read one two three; echo $$three) )
VERSION := $(SVN_VERSION_MAJOR).$(SVN_VERSION_MINOR).$(SVN_VERSION_MICRO)
RELEASE := $(shell svn st -v $(SPECFILE) | cut -b 10- | ( read one two ; echo $$one ) )
OSREL := $(shell ./find-osrel | ( read one two ; echo $$one ))
ARCH := $(shell arch |(read one two; echo $$one))

SOURCE_RPM_DIR := $(HOME)/$(RPMDIR)/sources/$(NAME)-$(VERSION)-$(RELEASE)

RPMDIRS := \
	$(HOME)/$(RPMDIR) \
	$(HOME)/$(RPMDIR)/build \
	$(HOME)/$(RPMDIR)/rpms \
	$(HOME)/$(RPMDIR)/rpms/i386 \
	$(HOME)/$(RPMDIR)/rpms/i486 \
	$(HOME)/$(RPMDIR)/rpms/i586 \
	$(HOME)/$(RPMDIR)/rpms/i686 \
	$(HOME)/$(RPMDIR)/rpms/noarch \
	$(HOME)/$(RPMDIR)/rpms/ppc \
	$(HOME)/$(RPMDIR)/rpms/sparc \
	$(HOME)/$(RPMDIR)/sources \
	$(HOME)/$(RPMDIR)/srpms \
	$(HOME)/$(RPMDIR)/tmp

all : rpm

subversion rpm : check_release build_srpm_files build_rpm_files brand_files

check_release :
	@if [ "$(RELEASE)"x = "x" ]; \
	   then \
		echo "Please specifiy RELEASE with 'make RELEASE=<number>'"; \
		exit 1; \
	    else \
		exit 0; \
	fi
	@echo "Making $(NAME)-$(VERSION)-$(RELEASE) (S)RPM for $(OSREL)  ..."

build_rpm_files : build_srpm_files
	cd $(SOURCE_RPM_DIR); rpmbuild -ba $(SPECFILE)

brand_files :
	cd $(SOURCE_RPM_DIR)/../../rpms/$(ARCH); for oldfile in *-$(VERSION)-$(RELEASE)*; do newfile=`echo $${oldfile} | sed -e "s/-$(VERSION)-$(RELEASE).$(ARCH).rpm/-$(VERSION)-$(RELEASE).$(OSREL).$(ARCH).rpm/"`; echo Moving $${oldfile} to $${newfile}; mv -f $${oldfile} $${newfile}; done
	cd $(SOURCE_RPM_DIR)/../../srpms; for oldfile in *-$(VERSION)-$(RELEASE)*; do newfile=`echo $${oldfile} | sed -e "s/-$(VERSION)-$(RELEASE).src.rpm/-$(VERSION)-$(RELEASE).$(OSREL).src.rpm/"`; echo Moving $${oldfile} to $${newfile}; mv -f $${oldfile} $${newfile}; done

build_srpm_files : $(HOME)/.rpmmacros $(HOME)/$(RPMDIR) subversion.spec
	rm -rf $(SOURCE_RPM_DIR)
	mkdir -p $(SOURCE_RPM_DIR)
	cp * $(SOURCE_RPM_DIR)
	sed -e 's/@RELEASE@/$(RELEASE)/' \
		-e 's/@VERSION@/$(VERSION)/' \
		< $(SPECFILE) > $(SOURCE_RPM_DIR)/$(SPECFILE)
	cd $(SOURCE_RPM_DIR); cp -r $(SVN_TOP_DIR) $(NAME)-$(VERSION); \
	   tar cfz $(NAME)-$(VERSION)-$(RELEASE).tar.gz $(NAME)-$(VERSION) --exclude=.svn; \
	   rm -rf $(NAME)-$(VERSION)

$(HOME)/.rpmmacros : .rpmmacros
	@if [ ! -f $(HOME)/.rpmmacros ]; \
	   then \
	   echo "Creating $(HOME)/.rpmmacros"; \
	   sed -e 's;@RPMSDIR@;$(HOME)/$(RPMDIR);' < .rpmmacros > $(HOME)/.rpmmacros; \
	   fi

$(HOME)/$(RPMDIR) :
	@if [ ! -d $(HOME)/$(RPMDIR) ]; \
		then \
		for i in $(RPMDIRS); \
			do \
			echo "Making directory $$i"; \
			mkdir -p $$i; \
			done; \
	   fi
