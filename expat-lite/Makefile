#
# quick Makefile for Expat (-lite)
#

OBJS=hashtable.o xmlparse.o xmlrole.o xmltok.o
HDRS=asciitab.h hashtable.h iasciitab.h latin1tab.h nametab.h \
	utf8tab.h xmldef.h xmlparse.h xmlrole.h xmltok.h xmltok_impl.h

### still need to set up the byte-order. see xmldef.h
CFLAGS=-DXML_NS

AR=ar
RANLIB=ranlib
RM=rm

all: libexpat.a

clean:
	rm -f *.o *.so *.a

libexpat.a: $(OBJS)
	$(RM) -f $@
	$(AR) cr $@ $(OBJS)
	$(RANLIB) $@

$(OBJS): $(HDRS)
