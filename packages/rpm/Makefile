##########################################################################
# Build an RPM from the current spec file and subversion directory tree. #
#                                                                        #
# Note: The RPM and Makefile are set up to generate RPMS as a non-root   #
# user in the user's home directory.                                     #
##########################################################################

RPMDIR := rpms
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
	@echo "Usage: Make rpm RevisionNumber=<Number> APR_TAR_FILE=<arp_tar_file> NEON_TAR_FILE=<neon_tar_file>"

rpm : build_srpm
	echo "Making RPM Revision Number $(RevisionNumber)..."

build_srpm : $(HOME)/.rpmmacros $(HOME)/$(RPMDIR) subversion.spec

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
