# aclocal.m4: Supplementary macros used by Subversion's configure.in
#
# These are here rather than directly in configure.in, since this prevents
# comments in the macro files being copied into configure.in, producing
# useless bloat. (This is significant - a 12kB reduction in size!)

# Include macros distributed by the APR project
sinclude(build/ac-macros/find_apr.m4)
sinclude(build/ac-macros/find_apu.m4)

# Include Subversion's own custom macros
sinclude(build/ac-macros/svn-macros.m4)
sinclude(build/ac-macros/apr.m4)
sinclude(build/ac-macros/aprutil.m4)
sinclude(build/ac-macros/neon.m4)
sinclude(build/ac-macros/serf.m4)
sinclude(build/ac-macros/berkeley-db.m4)
sinclude(build/ac-macros/svn-apache.m4)
sinclude(build/ac-macros/java.m4)
sinclude(build/ac-macros/swig.m4)

# Include the libtool macros
sinclude(build/libtool.m4)

