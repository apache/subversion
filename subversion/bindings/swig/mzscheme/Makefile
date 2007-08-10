.PHONY: shared-magic clean 
ALDTOP=../../../
APRTOP=../../../../
ALDPATH= -L/usr/lib -L$(ALDTOP)libsvn_subr/.libs -L$(ALDTOP)libsvn_client/.libs -L$(ALDTOP)libsvn_delta/.libs -L$(ALDTOP)libsvn_fs/.libs -L$(ALDTOP)libsvn_ra/.libs -L$(ALDTOP)libsvn_repos/.libs -L$(ALDTOP)libsvn_wc/.libs -L$(ALDTOP)libsvn_diff/.libs -L$(ALDTOP)libsvn_subr/.libs -L$(ALDTOP)libsvn_ra_local/.libs -L$(ALDTOP)libsvn_ra_svn/.libs -L$(ALDTOP)libsvn_ra_neon/.libs -L$(ALDTOP)libsvn_fs_fs/.libs -L$(APRTOP)apr-util/.libs -L$(APRTOP)apr-util/uri/.libs -L$(APRTOP)apr/.libs  -L$(ALDTOP)libsvn_subr/.libs
ALIBS= -lapr-0 -laprutil-0 -lapr-1 -laprutil-1 -lsvn_subr-1 -lsvn_client-1 -lsvn_delta-1 -lsvn_fs-1 -lsvn_ra-1 -lsvn_ra_neon-1 -lsvn_ra_local-1 -lsvn_repos-1 -lsvn_wc-1 -lsvn_diff-1 -lsvn_subr-1 
shared-magic: core.so svn_client.so svn_delta.so svn_fs.so svn_ra.so svn_repos.so svn_wc.so 
	@echo "building magic"

%.so: %.o
	@echo "building $< from $@"
	$(CC) $(CFLAGS) -shared  -o $@ -L/usr/local/lib -L/usr/lib -lrt -lm -lz -lc -lpthread -lcrypt -lnsl -ldl $<  $(ALDPATH) $(ALIBS)

clean:
	rm *.so