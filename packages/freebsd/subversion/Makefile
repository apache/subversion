# New ports collection makefile for:	subversion
# Date created:				10 September 2001
# Whom:					rooneg@electricjellyfish.net
#
# $FreeBSD$

PORTNAME=	subversion
PORTVERSION=	r1302
CATEGORIES=	devel
MASTER_SITES=	http://www.tigris.org/files/15/48/

MAINTAINER=	rooneg@electricjellyfish.net

LIB_DEPENDS=	db4:${PORTSDIR}/databases/db4 \
		neon.18:${PORTSDIR}/www/neon \
		expat:${PORTSDIR}/textproc/expat \
		apr:${PORTSDIR}/devel/apr-snapshot

RUN_DEPENDS=	gdiff:${PORTSDIR}/textproc/diffutils \
		gpatch:${PORTSDIR}/devel/patch

BUILD_DEPENDS=	gdiff:${PORTSDIR}/textproc/diffutils \
		gpatch:${PORTSDIR}/devel/patch \
		python:${PORTSDIR}/lang/python

MAN1=		svn.1

WRKSRC=	${WRKDIR}/subversion-r1302
GNU_CONFIGURE=	yes
CONFIGURE_ARGS= --with-ssl --with-neon=${LOCALBASE}

post-extract:
	# work around libdb4's name.
	@${PERL} -pi -e 's/ldb/ldb4/g' ${WRKSRC}/configure

	# axe the neon subdir because otherwise svn complains when trying to
	# link against our installed version.
	@${RM} -r ${WRKSRC}/neon

post-configure:
	# use our installed expat, rather than the expat-lite in svn's tree
	@${PATCH} < files/build.conf.diff
	(cd ${WRKSRC} && ./gen-make.py -s build.conf)

	# make sure we don't install the swig stuff, even if it was found
	@${PERL} -pi -e 's/install-swig-py-lib//g' ${WRKSRC}/Makefile
	@${PERL} -pi -e 's/swig-py-lib//g' ${WRKSRC}/Makefile

.include <bsd.port.mk>
