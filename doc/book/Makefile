XSLTPROC = xsltproc
INSTALL_DIR = $(DESTDIR)/usr/share/doc/subversion
INSTALL = install

## You shouldn't normally need to edit anything below here.
SHELL = /bin/sh

BOOK_TOP = .
BOOK_HTML_CHUNK_DIR = $(BOOK_DIR)/html-chunk
BOOK_DIR = ${BOOK_TOP}/book
BOOK_HTML_TARGET = $(BOOK_DIR)/book.html
BOOK_PDF_TARGET = $(BOOK_DIR)/book.pdf
BOOK_PS_TARGET = $(BOOK_DIR)/book.ps
BOOK_FO_TARGET = $(BOOK_DIR)/book.fo
BOOK_XML_SOURCE = $(BOOK_DIR)/book.xml
BOOK_ALL_SOURCE = $(BOOK_DIR)/*.xml
BOOK_INSTALL_DIR = $(INSTALL_DIR)/book

MDOCS_DIR = ${BOOK_TOP}/misc-docs
MDOCS_HTML_TARGET = $(MDOCS_DIR)/misc-docs.html
MDOCS_PDF_TARGET = $(MDOCS_DIR)/misc-docs.pdf
MDOCS_PS_TARGET = $(MDOCS_DIR)/misc-docs.ps
MDOCS_FO_TARGET = $(MDOCS_DIR)/misc-docs.fo
MDOCS_XML_SOURCE = $(MDOCS_DIR)/misc-docs.xml
MDOCS_ALL_SOURCE = $(MDOCS_DIR)/*.xml
MDOCS_INSTALL_DIR = $(INSTALL_DIR)/misc-docs

XSL_DIR = ${BOOK_TOP}/tools/xsl
XSL_FO = ${BOOK_TOP}/tools/fo-stylesheet.xsl
XSL_HTML = $(XSL_DIR)/html/docbook.xsl
XSL_HTML_CHUNK = $(XSL_DIR)/html/chunk.xsl

RUN_FOP = ${BOOK_TOP}/tools/bin/run-fop.sh

# Book xsltproc options for HTML output
BOOK_HTML_XSLTPROC_OPTS = --stringparam html.stylesheet styles.css
BOOK_CHUNK_HTML_XSLTPROC_OPTS = stringparam html.stylesheet styles.css

# Book xsltproc options for PDF and PostScript output
# BOOK_PDF_XSLTPROC_OPTS = --stringparam page.height 9in --stringparam page.width 6.4in
# BOOK_PS_XSLTPROC_OPTS = --stringparam page.height 9in --stringparam page.width 6.4in

all: all-html all-pdf all-ps

install: install-book install-misc-docs

all-html: book-html book-html-chunk misc-docs-html

all-pdf: book-pdf misc-docs-pdf

all-ps: book-ps misc-docs-ps

all-book: book-html book-html-chunk book-pdf book-ps

install-book: install-book-html install-book-html-chunk install-book-pdf install-book-ps

all-misc-docs: misc-docs-html misc-docs-pdf book-ps

install-misc-docs: install-misc-html install-misc-pdf \
                   install-misc-ps

clean: book-clean misc-docs-clean


book-html: $(BOOK_HTML_TARGET)

$(BOOK_HTML_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(BOOK_HTML_XSLTPROC_OPTS) \
           --output $(BOOK_HTML_TARGET) $(XSL_HTML) $(BOOK_XML_SOURCE)

## This trailing slash is essential that xsltproc will output pages to the dir
book-html-chunk: $(BOOK_ALL_SOURCE)
	mkdir -p $(BOOK_HTML_CHUNK_DIR)
	$(XSLTPROC) $(BOOK_HTML_XSLTPROC_OPTS) \
           --output $(BOOK_HTML_CHUNK_DIR)/ \
	   $(XSL_HTML_CHUNK) $(BOOK_XML_SOURCE)
	cp $(BOOK_DIR)/styles.css $(BOOK_HTML_CHUNK_DIR)
	cp $(BOOK_DIR)/images/*.png $(BOOK_HTML_CHUNK_DIR)

book-pdf: $(BOOK_PDF_TARGET)

book-ps: $(BOOK_PS_TARGET)

$(BOOK_PDF_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(BOOK_PDF_XSLTPROC_OPTS) \
	   --output $(BOOK_FO_TARGET) $(XSL_FO) $(BOOK_XML_SOURCE)
	$(RUN_FOP) $(BOOK_TOP) -fo $(BOOK_FO_TARGET) -pdf $(BOOK_PDF_TARGET)

$(BOOK_PS_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(BOOK_PS_XSLTPROC_OPTS) \
	   --output $(BOOK_FO_TARGET) $(XSL_FO) $(BOOK_XML_SOURCE)
	$(RUN_FOP) $(BOOK_TOP) -fo $(BOOK_FO_TARGET) -ps $(BOOK_PS_TARGET)

$(BOOK_INSTALL_DIR):
	$(INSTALL) -d $(BOOK_INSTALL_DIR)

install-book-html: $(BOOK_HTML_TARGET)
	$(INSTALL) -d $(BOOK_INSTALL_DIR)/images
	$(INSTALL) $(BOOK_HTML_TARGET) $(BOOK_INSTALL_DIR)
	$(INSTALL) $(BOOK_DIR)/images/*.png $(BOOK_INSTALL_DIR)/images

## index.html is created last
install-book-html-chunk: $(BOOK_HTML_CHUNK_DIR)/index.html
	$(INSTALL) -d $(BOOK_INSTALL_DIR)/images
	$(INSTALL) $(BOOK_HTML_CHUNK_DIR)/*.html $(BOOK_INSTALL_DIR)
	$(INSTALL) $(BOOK_DIR)/styles.css $(BOOK_INSTALL_DIR)
	$(INSTALL) $(BOOK_DIR)/images/*.png $(BOOK_INSTALL_DIR)/images

install-book-pdf: $(BOOK_PDF_TARGET) $(BOOK_INSTALL_DIR)
	$(INSTALL) $(BOOK_PDF_TARGET) $(BOOK_INSTALL_DIR)

install-book-ps: $(BOOK_PS_TARGET) $(BOOK_INSTALL_DIR)
	$(INSTALL) $(BOOK_PS_TARGET) $(BOOK_INSTALL_DIR)

book-clean:
	rm -f $(BOOK_HTML_TARGET) $(BOOK_FO_TARGET)
	rm -rf $(BOOK_HTML_CHUNK_DIR)
	rm -f $(BOOK_PDF_TARGET) $(BOOK_PS_TARGET) 

misc-docs-html: $(MDOCS_HTML_TARGET)

$(MDOCS_HTML_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_HTML) $(MDOCS_XML_SOURCE) > $(MDOCS_HTML_TARGET)

misc-docs-pdf: $(MDOCS_PDF_TARGET)

misc-docs-ps: $(MDOCS_PS_TARGET)

$(MDOCS_PDF_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(MDOCS_XML_SOURCE) > $(MDOCS_FO_TARGET)
	$(RUN_FOP) $(BOOK_TOP) -fo $(MDOCS_FO_TARGET) -pdf $(MDOCS_PDF_TARGET)

$(MDOCS_PS_TARGET): $(MDOCS_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(MDOCS_XML_SOURCE) > $(MDOCS_FO_TARGET)
	$(RUN_FOP) $(BOOK_TOP) -fo $(MDOCS_FO_TARGET) -ps $(MDOCS_PS_TARGET)

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


