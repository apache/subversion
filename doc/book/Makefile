XSLTPROC = xsltproc
RUN_FOP = tools/bin/run-fop.sh

## You shouldn't normally need to edit anything below here.
SHELL = /bin/sh

BOOK_TOP = .
BOOK_DIR = book
BOOK_HTML_TARGET = $(BOOK_DIR)/book.html
BOOK_PDF_TARGET = $(BOOK_DIR)/book.pdf
BOOK_PS_TARGET = $(BOOK_DIR)/book.ps
BOOK_FO_TARGET = $(BOOK_DIR)/book.fo
BOOK_XML_SOURCE = $(BOOK_DIR)/book.xml
BOOK_ALL_SOURCE = $(BOOK_DIR)/*.xml

MDOCS_DIR = misc-docs
MDOCS_HTML_TARGET = $(MDOCS_DIR)/misc-docs.html
MDOCS_PDF_TARGET = $(MDOCS_DIR)/misc-docs.pdf
MDOCS_PS_TARGET = $(MDOCS_DIR)/misc-docs.ps
MDOCS_FO_TARGET = $(MDOCS_DIR)/misc-docs.fo
MDOCS_XML_SOURCE = $(MDOCS_DIR)/misc-docs.xml
MDOCS_ALL_SOURCE = $(MDOCS_DIR)/*.xml

XSL_DIR = tools/xsl
XSL_FO = $(XSL_DIR)/fo/docbook.xsl
XSL_HTML = $(XSL_DIR)/html/docbook.xsl


all: all-html all-pdf all-ps

all-html: book-html misc-docs-html

all-pdf: book-pdf misc-docs-pdf

all-ps: book-ps misc-docs-ps

all-book: book-html book-pdf book-ps

all-misc-docs: misc-docs-html misc-docs-pdf book-ps

clean: book-clean misc-docs-clean


book-html: $(BOOK_HTML_TARGET)

$(BOOK_HTML_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(XSL_HTML) $(BOOK_XML_SOURCE) > $(BOOK_HTML_TARGET)

book-pdf: $(BOOK_PDF_TARGET)

book-ps: $(BOOK_PS_TARGET)

$(BOOK_PDF_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(BOOK_XML_SOURCE) > $(BOOK_FO_TARGET)
	$(RUN_FOP) $(BOOK_TOP) -fo $(BOOK_FO_TARGET) -pdf $(BOOK_PDF_TARGET)

$(BOOK_PS_TARGET): $(BOOK_ALL_SOURCE)
	$(XSLTPROC) $(XSL_FO) $(BOOK_XML_SOURCE) > $(BOOK_FO_TARGET)
	$(RUN_FOP) $(BOOK_TOP) -fo $(BOOK_FO_TARGET) -ps $(BOOK_PS_TARGET)

book-clean:
	rm -f $(BOOK_HTML_TARGET) $(BOOK_FO_TARGET)
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
