# New ports collection makefile for: apr-snapshot
# Date created:		19 February 2002
# Whom:			Garrett Rooney <rooneg@electricjellyfish.net>
#
# $FreeBSD$
#

PORTNAME=	apr-snapshot
PORTVERSION=	20020219232325
CATEGORIES=	devel

DISTFILES=	apr_20020219232325.tar.gz apr-util_20020219232336.tar.gz
MASTER_SITES=	http://www.apache.org/dist/apr/not-released/

MAINTAINER=	rooneg@electricjellyfish.net

BUILD_DEPENDS=	autoconf:${PORTSDIR}/devel/autoconf \
		libtool:${PORTSDIR}/devel/libtool

GNU_CONFIGURE=	yes

WRKSRC=		${WRKDIR}

pre-configure:
	(cd ${WRKSRC}/apr/ && ./buildconf)
	(cd ${WRKSRC}/apr-util/ && ./buildconf)

do-configure:
	(cd ${WRKSRC}/apr/ && ./configure)
	(cd ${WRKSRC}/apr-util/ && ./configure --with-apr=../apr/)

do-build:
	(cd ${WRKSRC}/apr/ && make)
	(cd ${WRKSRC}/apr-util/ && make)

do-install:
	(cd ${WRKSRC}/apr/ && make install)
	(cd ${WRKSRC}/apr-util/ && make install)

.include <bsd.port.mk>
