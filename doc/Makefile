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
MDOCS_HTML_TARGET = $(MDOCS_DIR)/misc-docs.html
MDOCS_PDF_TARGET = $(MDOCS_DIR)/misc-docs.pdf
MDOCS_PS_TARGET = $(MDOCS_DIR)/misc-docs.ps
MDOCS_FO_TARGET = $(MDOCS_DIR)/misc-docs.fo
MDOCS_XML_SOURCE = $(MDOCS_DIR)/misc-docs.xml
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

# Grouping targets
all: all-misc-docs
all-misc-docs: misc-docs-html misc-docs-pdf misc-docs-ps
all-html: misc-docs-html
all-pdf: misc-docs-pdf
all-ps: misc-docs-ps

install: install-misc-docs
install-misc-docs: install-misc-docs-html \
	      install-misc-docs-pdf install-misc-docs-ps

clean: misc-docs-clean

# Build targets
misc-docs-html: $(MDOCS_HTML_TARGET)
$(MDOCS_HTML_TARGET): $(MDOCS_ALL_SOURCE)
	$(ENSURE_XSL)
	$(XSLTPROC) $(MDOCS_HTML_XSLTPROC_OPTS) --output $(MDOCS_HTML_TARGET) \
	  $(TOOLS_DIR)/html-stylesheet.xsl $(MDOCS_XML_SOURCE)

$(MDOCS_FO_TARGET): $(MDOCS_ALL_SOURCE)
	$(ENSURE_XSL)
	$(XSLTPROC) $(MDOCS_FO_XSLTPROC_OPTS) --output $(MDOCS_FO_TARGET) \
	  $(TOOLS_DIR)/fo-stylesheet.xsl $(MDOCS_XML_SOURCE)

misc-docs-pdf: $(MDOCS_PDF_TARGET)
$(MDOCS_PDF_TARGET): $(MDOCS_FO_TARGET)
	$(TOOLS_DIR)/bin/run-fop.sh . -fo $(MDOCS_FO_TARGET) \
	  -pdf $(MDOCS_PDF_TARGET)

misc-docs-ps: $(MDOCS_PS_TARGET)
$(MDOCS_PS_TARGET): $(MDOCS_FO_TARGET)
	$(TOOLS_DIR)/bin/run-fop.sh . -fo $(MDOCS_FO_TARGET) \
	  -ps $(MDOCS_PS_TARGET)

# Install targets
$(MDOCS_INSTALL_DIR):
	$(INSTALL) -d $(MDOCS_INSTALL_DIR)

install-misc-docs-html: $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-docs-pdf: $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-docs-ps: $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)

# Clean targets
misc-docs-clean:
	rm -f $(MDOCS_HTML_TARGET) $(MDOCS_FO_TARGET)
	rm -f $(MDOCS_PDF_TARGET) $(MDOCS_PS_TARGET) 

# Utility targets
valid:
	$(XMLLINT) --noout --nonet --valid $(MDOCS_XML_SOURCE)

