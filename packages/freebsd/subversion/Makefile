# New ports collection makefile for:	subversion
# Date created:				10 September 2001
# Whom:					rooneg@electricjellyfish.net
#
# $FreeBSD$

PORTNAME=	subversion
PORTVERSION=	r727
CATEGORIES=	devel
MASTER_SITES=	http://www.tigris.org/files/15/38/

MAINTAINER=	rooneg@electricjellyfish.net

LIB_DEPENDS=	db4:${PORTSDIR}/databases/db4 \
		neon.18:${PORTSDIR}/www/neon

BUILD_DEPENDS=	gdiff:${PORTSDIR}/textproc/diffutils \
		gpatch:${PORTSDIR}/devel/patch \
		libtool:${PORTSDIR}/devel/libtool

INSTALLS_SHLIBS=	yes
LDCONFIG_DIRS=	%%PREFIX%%/lib/subversion
MAN1=		svn.1

WRKSRC=	${WRKDIR}/subversion-r727
GNU_CONFIGURE=	yes
CONFIGURE_ARGS= --enable-maintainer-mode --disable-shared \
		--libdir=${PREFIX}/lib/subversion         \
		--includedir=${PREFIX}/include/subversion
CONFIGURE_TARGET=	--target=${MACHINE_ARCH}-unknown-freebsd${OSREL}

post-extract: fix-configure axe-neon-dir

fix-configure:
	@${PERL} -pi -e 's/ldb/ldb4/g' ${WRKSRC}/configure

axe-neon-dir:
	@${RM} -r ${WRKSRC}/neon

.include <bsd.port.mk>
