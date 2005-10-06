XSLTPROC = xsltproc
INSTALL_DIR = $(DESTDIR)/usr/share/doc/subversion
INSTALL = install

## You shouldn't normally need to edit anything below here.
SHELL = /bin/sh
SVNVERSION = svnversion

DOC_TOP = .

MDOCS_DIR = ${DOC_TOP}/misc-docs
MDOCS_HTML_TARGET = $(MDOCS_DIR)/misc-docs.html
MDOCS_PDF_TARGET = $(MDOCS_DIR)/misc-docs.pdf
MDOCS_PS_TARGET = $(MDOCS_DIR)/misc-docs.ps
MDOCS_FO_TARGET = $(MDOCS_DIR)/misc-docs.fo
MDOCS_XML_SOURCE = $(MDOCS_DIR)/misc-docs.xml
MDOCS_ALL_SOURCE = $(MDOCS_DIR)/*.xml
MDOCS_INSTALL_DIR = $(INSTALL_DIR)/misc-docs

XSL_FO = ${DOC_TOP}/tools/fo-stylesheet.xsl
XSL_HTML = $(DOC_TOP)/tools/html-stylesheet.xsl
XSL_HTML_CHUNK = $(DOC_TOP)/tools/chunk-stylesheet.xsl

RUN_FOP = ${DOC_TOP}/tools/bin/run-fop.sh

# Book xsltproc options for HTML output
# Note: --stringparam arguments no longer go here; 
# see tools/html-stylesheet.xsl and tools/chunk-stylesheet.xsl
DOC_HTML_XSLTPROC_OPTS = 

# Book xsltproc options for PDF and PostScript output
# DOC_PDF_XSLTPROC_OPTS = --stringparam page.height 9in --stringparam page.width 6.4in
# DOC_PS_XSLTPROC_OPTS = --stringparam page.height 9in --stringparam page.width 6.4in

# Uncomment the following line if you'd like to print on A4 paper
# DOC_PDF_XSLTPROC_OPTS = --stringparam paper.type A4

all: all-html all-pdf all-ps

install: install-misc-docs

all-html: misc-docs-html

all-pdf: misc-docs-pdf

all-ps: misc-docs-ps

all-misc-docs: misc-docs-html misc-docs-pdf

install-misc-docs: install-misc-html install-misc-pdf \
                   install-misc-ps

clean: misc-docs-clean

misc-docs-html: $(MDOCS_HTML_TARGET)

$(MDOCS_HTML_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_HTML) $(MDOCS_XML_SOURCE) > $(MDOCS_HTML_TARGET)

misc-docs-pdf: $(MDOCS_PDF_TARGET)

misc-docs-ps: $(MDOCS_PS_TARGET)

$(MDOCS_PDF_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(MDOCS_XML_SOURCE) > $(MDOCS_FO_TARGET)
	$(RUN_FOP) $(DOC_TOP) -fo $(MDOCS_FO_TARGET) -pdf $(MDOCS_PDF_TARGET)

$(MDOCS_PS_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(MDOCS_XML_SOURCE) > $(MDOCS_FO_TARGET)
	$(RUN_FOP) $(DOC_TOP) -fo $(MDOCS_FO_TARGET) -ps $(MDOCS_PS_TARGET)

misc-docs-clean:
	rm -f $(MDOCS_HTML_TARGET) $(MDOCS_FO_TARGET)
	rm -f $(MDOCS_PDF_TARGET) $(MDOCS_PS_TARGET)

$(MDOCS_INSTALL_DIR):
	$(INSTALL) -d $(MDOCS_INSTALL_DIR)

install-misc-html: $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_HTML_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-pdf: $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PDF_TARGET) $(MDOCS_INSTALL_DIR)

install-misc-ps: $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)
	$(INSTALL) $(MDOCS_PS_TARGET) $(MDOCS_INSTALL_DIR)
