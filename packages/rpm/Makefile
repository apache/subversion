##########################################################################
# Build an RPM from the current spec file and subversion directory tree. #
#                                                                        #
# Note: The RPM and Makefile are set up to GENERATE RPMS as a non-root   #
# user in the user's home directory.  Of course to INSTALL the resulting #
# binary RPM the user has to log in as root.                             #
##########################################################################

SPECFILE := subversion.spec

RPMDIR := rpms

SVN_TOP_DIR := $(shell pwd)/../..

NAME := $(shell grep '^Name:' < $(SPECFILE) | sed -e 's/^Name: //')
VERSION := $(shell grep '^Version: ' < $(SPECFILE) | sed 's/^Version: //')
RELEASE := $(RevisionNumber)

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

all :
	@echo "Usage: Make rpm RevisionNumber=<Number>"

rpm : build_srpm_files build_rpm
	@echo "Making $(NAME)-$(VERSION)-$(RELEASE) (S)RPM..."

build_rpm : build_srpm_files
	cd $(SOURCE_RPM_DIR); rpm -ba $(SPECFILE)

build_srpm_files : $(HOME)/.rpmmacros $(HOME)/$(RPMDIR) subversion.spec
	rm -rf $(SOURCE_RPM_DIR)
	mkdir -p $(SOURCE_RPM_DIR)
	cp * $(SOURCE_RPM_DIR)
	sed -e 's/@RELEASE@/$(RELEASE)/' < $(SPECFILE) > $(SOURCE_RPM_DIR)/$(SPECFILE)
	cd $(SOURCE_RPM_DIR); cp -r $(SVN_TOP_DIR) $(NAME)-$(VERSION); \
	   tar cfz $(NAME)-$(VERSION)-$(RELEASE).tar.gz $(NAME)-$(VERSION) --exclude .svn; \
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
