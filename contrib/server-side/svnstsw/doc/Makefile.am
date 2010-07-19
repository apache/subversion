# Copyright (c) 2008 BBN Technologies Corp.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of BBN Technologies nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY BBN TECHNOLOGIES AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL BBN TECHNOLOGIES OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

AM_CPPFLAGS = $(CONFIGURECPPFLAGS)
AM_CFLAGS = $(CONFIGURECFLAGS)
AM_CXXFLAGS = $(CONFIGURECXXFLAGS)
AM_LDFLAGS = $(CONFIGURELDFLAGS)
AM_LIBTOOLFLAGS = $(CONFIGURELIBTOOLFLAGS)

man_MANS = \
	man/svnstsw.8 \
	man/libsvnstsw.3 \
	man/svnstsw_exec_svnserve.3 \
	man/svnstsw_get_tunnel_user_name.3 \
	man/svnstsw_fso_is_changeable.3

#all-local: html-local
install-data-local: install-html-local

html-local:
	@if test "x$(DOXYGEN)" = "x" ; then \
		echo "Error:  Doxygen is not installed" >&2 ; \
		false ; \
	fi
	@if test "x$(DOXYFILE_HAVE_DOT)" != "xYES" ; then \
		echo "" >&2 ; \
		echo "+---------------------------------------------------------+" >&2 ; \
		echo "| WARNING:  dot (from Graphviz) was not detected; Doxygen |" >&2 ; \
		echo "| documentation will not contain any graphs.  If you have |" >&2 ; \
		echo "| dot installed on your system, you may specify the path  |" >&2 ; \
		echo "| to dot using configure's '--with-dot' argument.         |" >&2 ; \
		echo "+---------------------------------------------------------+" >&2 ; \
		echo "" >&2 ; \
	fi
	@echo "Generating Doxygen HTML documentation."
	@echo "This may take a while..."
	$(DOXYGEN) Doxyfile-html

install-html-local:
	if test -e html/index.html ; then \
		$(INSTALL) -d "$(DESTDIR)$(htmldir)/html"; \
		$(INSTALL_DATA) -t "$(DESTDIR)$(htmldir)/html" html/*; \
	fi

clean-local:
	if test -d html ; then \
		rm -rf html; \
	fi

uninstall-local:
	if test -d "$(DESTDIR)$(htmldir)/html" ; then \
		rm -rf "$(DESTDIR)$(htmldir)/html"; \
	fi
