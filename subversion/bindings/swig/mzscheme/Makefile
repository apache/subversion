.PHONY: shared-magic clean all
ALDTOP=../../../
APRTOP=../../../../
ALDPATH= -L./ -L./.libs -L/usr/lib -L$(ALDTOP)libsvn_subr/.libs -L$(ALDTOP)libsvn_client/.libs -L$(ALDTOP)libsvn_delta/.libs -L$(ALDTOP)/libsvn_fs_fs/.libs -L$(ALDTOP)libsvn_fs/.libs -L$(ALDTOP)libsvn_ra/.libs -L$(ALDTOP)libsvn_repos/.libs -L$(ALDTOP)libsvn_wc/.libs -L$(ALDTOP)libsvn_diff/.libs -L$(ALDTOP)libsvn_subr/.libs -L$(ALDTOP)libsvn_ra_local/.libs -L$(ALDTOP)libsvn_ra_svn/.libs -L$(ALDTOP)libsvn_fs_util/.libs -L$(ALDTOP)libsvn_ra_neon/.libs -L$(ALDTOP)libsvn_fs_fs/.libs -L$(APRTOP)apr-util/.libs -L$(APRTOP)apr-util/uri/.libs -L$(APRTOP)apr/.libs  -L$(ALDTOP)libsvn_subr/.libs -L./libsvn_swig_mzscheme/ -L./libsvn_swig_mzscheme/.libs
ALIBS= -lsvn_subr-1 -lsvn_fs_util-1 -lsvn_fs-1 -lsvn_fs_fs-1 -lsvn_fs-1 -lsvn_delta-1 -lneon -lapr-1 -laprutil-1  -lsvn_delta-1 -lsvn_subr-1 -lsvn_client-1  -lsvn_delta-1 -lsvn_fs-1 -lsvn_ra_svn-1 -lsvn_ra-1 -lsvn_ra_neon-1 -lsvn_ra-1 -lsvn_ra_local-1 -lsvn_ra_svn-1  -lsvn_repos-1 -lsvn_wc-1 -lsvn_diff-1 -lsvn_subr-1 -lsvn_delta-1 -lsvn_fs-1 -lsvn_ra-1 -lsvn_fs-1 -lsvn_fs_fs-1 -lsvn_delta-1 -lsvn_fs_fs-1 -lsvn_fs_util-1 -lsvn_swig_mzscheme-1 -lsvn_subr-1
all: shared-magic svnalphaschemebindings.plt README

svnalphaschemebindings.plt : shared-magic
	@echo "creating plt bundle"
	mzc --plt svnalphaschemebindings.plt -- `find ./`

shared-magic: core.so svn_client.so svn_delta.so svn_fs.so svn_ra.so svn_repos.so svn_wc.so 
	@echo "building magic"


%.so: %.o
	@echo "building $< from $@"
	$(CC) $(CFLAGS) -shared  -o $@ -L/usr/local/lib -L/usr/lib -lrt -lm -lz -lc -lpthread -lcrypt -lnsl -ldl $< $(ALDPATH) $(ALIBS)

README:
	perl make_readme.pl

clean:
	rm *.so
	rm *.lo
	rm *.c
	rm svnalphaschemebindings.plt
	rm README
