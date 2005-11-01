# Paths which you may wish to customize:
XSLTPROC = xsltproc
XMLLINT = xmllint
INSTALL_DIR = $(DESTDIR)/usr/share/doc/subversion
INSTALL = install
SVNVERSION = svnversion

# You should not normally need to edit anything below here.
SHELL = /bin/sh

TOOLS_DIR = tools
MDOCS_DIR = misc-docs
MDOCS_HTML_CHUNK_DIR = $(MDOCS_DIR)/html-chunk
MDOCS_HTML_TARGET = $(MDOCS_DIR)/svn-misc-docs.html
# In the HTML chunk build, index.html is created last, so serves as an 
# acceptable timestamp file for the entire multi-file output.
MDOCS_HTML_CHUNK_TARGET = $(MDOCS_HTML_CHUNK_DIR)/index.html
MDOCS_PDF_TARGET = $(MDOCS_DIR)/svn-misc-docs.pdf
MDOCS_PS_TARGET = $(MDOCS_DIR)/svn-misc-docs.ps
MDOCS_FO_TARGET = $(MDOCS_DIR)/svn-misc-docs.fo
MDOCS_XML_SOURCE = $(MDOCS_DIR)/misc-docs.xml
MDOCS_VERSION_SOURCE = $(MDOCS_DIR)/version.xml
MDOCS_ALL_SOURCE = $(MDOCS_DIR)/*.xml
MDOCS_INSTALL_DIR = $(INSTALL_DIR)/misc-docs

ENSURE_XSL = if ! test -e "$(TOOLS_DIR)/xsl"; \
	     then $(TOOLS_DIR)/bin/find-xsl.py; fi

# Customization hooks for xsltproc options
MDOCS_HTML_XSLTPROC_OPTS = 
MDOCS_FO_XSLTPROC_OPTS =
# FO example: --stringparam page.height 9in --stringparam page.width 6.4in

# Uncomment the following line if you'd like to print on A4 paper
# MDOCS_FO_XSLTPROC_OPTS = --stringparam paper.type A4

# We don't do locale-specific stuff for misc-docs, but leave this here anyway
# to reduce diffs against the book Makefile.
L10N_REVISION = Revision

# Grouping targets
all: all-misc-docs
all-misc-docs: misc-docs-html misc-docs-html-chunk misc-docs-pdf misc-docs-ps
all-html: misc-docs-html misc-docs-html-chunk
all-pdf: misc-docs-pdf
all-ps: misc-docs-ps

install: install-misc-docs
install-misc-docs: install-misc-docs-html install-misc-docs-html-chunk \
	      install-misc-docs-pdf install-misc-docs-ps

clean: misc-docs-clean

# Build targets
$(MDOCS_VERSION_SOURCE): misc-docs-version

misc-docs-version:
	@if $(SVNVERSION) . > /dev/null; then \
	  echo '<!ENTITY svn.version "$(L10N_REVISION) '`$(SVNVERSION) .`'">' \
	    > $(MDOCS_VERSION_SOURCE).tmp; \
	else \
	  echo '<!ENTITY svn.version "">' > $(MDOCS_VERSION_SOURCE).tmp; \
	fi
	@if cmp -s $(MDOCS_VERSION_SOURCE) $(MDOCS_VERSION_SOURCE).tmp; then \
	  rm $(MDOCS_VERSION_SOURCE).tmp; \
	else \
	  mv $(MDOCS_VERSION_SOURCE).tmp $(MDOCS_VERSION_SOURCE); \
	fi

misc-docs-html: $(MDOCS_HTML_TARGET)
$(MDOCS_HTML_TARGET): $(MDOCS_ALL_SOURCE) $(MDOCS_VERSION_SOURCE)
	$(ENSURE_XSL)
	$(XSLTPROC) $(MDOCS_HTML_XSLTPROC_OPTS) --output $(MDOCS_HTML_TARGET) \
	  $(TOOLS_DIR)/html-stylesheet.xsl $(MDOCS_XML_SOURCE)

# The trailing slash on the xsltproc --output option is essential to
# output pages into the directory
misc-docs-html-chunk: $(MDOCS_HTML_CHUNK_TARGET)
$(MDOCS_HTML_CHUNK_TARGET): $(MDOCS_ALL_SOURCE) $(MDOCS_VERSION_SOURCE)
	mkdir -p $(MDOCS_HTML_CHUNK_DIR)
	$(ENSURE_XSL)
	$(XSLTPROC) $(MDOCS_HTML_XSLTPROC_OPTS) \
           --output $(MDOCS_HTML_CHUNK_DIR)/ \
	   $(TOOLS_DIR)/chunk-stylesheet.xsl $(MDOCS_XML_SOURCE)

$(MDOCS_FO_TARGET): $(MDOCS_ALL_SOURCE) $(MDOCS_VERSION_SOURCE)
	$(ENSURE_XSL)
	$(XSLTPROC) $(MDOCS_FO_XSLTPROC_OPTS) --output $(MDOCS_FO_TARGET) \
	  $(TOOLS_DIR)/fo-stylesheet.xsl $(MDOCS_XML_SOURCE)

misc-docs-pdf: $(MDOCS_PDF_TARGET)
$(MDOCS_PDF_TARGET): $(MDOCS_FO_TARGET)
	$(TOOLS_DIR)/bin/run-fop.sh -fo $(MDOCS_FO_TARGET) \
	  -pdf $(MDOCS_PDF_TARGET)

misc-docs-ps: $(MDOCS_PS_TARGET)
$(MDOCS_PS_TARGET): $(MDOCS_FO_TARGET)
	$(TOOLS_DIR)/bin/run-fop.sh -fo $(MDOCS_FO_TARGET) \
	  -ps $(MDOCS_PS_TARGET)

# Install targets
$(MDOCS_INSTALL_DIR):
	$(INSTALL) -d $(MDOCS_INSTALL_DIR)

install-misc-docs-html: $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-docs-html-chunk: $(MDOCS_HTML_CHUNK_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_HTML_CHUNK_DIR)/*.html $(MDOCS_INSTALL_DIR)

install-misc-docs-pdf: $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-docs-ps: $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)

# Clean targets
misc-docs-clean:
	rm -f $(MDOCS_VERSION_SOURCE)
	rm -f $(MDOCS_HTML_TARGET) $(MDOCS_FO_TARGET)
	rm -rf $(MDOCS_HTML_CHUNK_DIR)
	rm -f $(MDOCS_PDF_TARGET) $(MDOCS_PS_TARGET) 

# Utility targets
valid: $(MDOCS_VERSION_SOURCE)
	$(XMLLINT) --noout --nonet --valid $(MDOCS_XML_SOURCE)

