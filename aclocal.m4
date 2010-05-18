# aclocal.m4: Supplementary macros used by Subversion's configure.ac
#
# These are here rather than directly in configure.ac, since this prevents
# comments in the macro files being copied into configure.ac, producing
# useless bloat. (This is significant - a 12kB reduction in size!)

# Include macros distributed by the APR project
sinclude(build/ac-macros/find_apr.m4)
sinclude(build/ac-macros/find_apu.m4)

# Include Subversion's own custom macros
sinclude(build/ac-macros/svn-macros.m4)

sinclude(build/ac-macros/apache.m4)
sinclude(build/ac-macros/apr.m4)
sinclude(build/ac-macros/aprutil.m4)
sinclude(build/ac-macros/apr_memcache.m4)
sinclude(build/ac-macros/berkeley-db.m4)
sinclude(build/ac-macros/ctypesgen.m4)
sinclude(build/ac-macros/java.m4)
sinclude(build/ac-macros/neon.m4)
sinclude(build/ac-macros/sasl.m4)
sinclude(build/ac-macros/serf.m4)
sinclude(build/ac-macros/sqlite.m4)
sinclude(build/ac-macros/swig.m4)
sinclude(build/ac-macros/zlib.m4)
sinclude(build/ac-macros/kwallet.m4)

# Include the libtool macros
sinclude(build/libtool.m4)
sinclude(build/ltoptions.m4)
sinclude(build/ltsugar.m4)
sinclude(build/ltversion.m4)
sinclude(build/lt~obsolete.m4)
