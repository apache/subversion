# Creates a makefile for building the svn library for Ruby.
# Assumes SVN has already been successfully installed
#
# Options: --with-svn-dir=<path to svn install>
#

if /mswin32/ =~ PLATFORM then
  require 'env'
else
  require 'Env'
end

require 'mkmf'	# Here's the ruby module that does the grunt work

dir_config('svn')

$CFLAGS << ' -I. '

$LDFLAGS << `apr-config --ldflags`.chop
$LOCAL_LIBS << `apr-config --libs`.chop
$CFLAGS << `apr-config --cflags`.chop
$CFLAGS << `apr-config --includes`.chop

$LDFLAGS << `svn-config --ldflags`.chop
$CFLAGS << `svn-config --cflags`.chop
$CFLAGS << `svn-config --includes`.chop

# ick...  svn-config doesn't seem to be adding in the directory the subversion 
# includes are in, so lets work around that for now.
$CFLAGS << ' -I' + `svn-config --prefix`.chop + '/include/subversion-1'

# Linux needs -lpthread.
if PLATFORM =~ /linux/ && $CFLAGS =~ /-pthread/ then
  have_library('pthread')
end

# Extra libraries needed to compile
libraries = [
              ['apr-0',        'apr_initialize'],
              ['aprutil-0',    'apr_bucket_alloc'],
              ['svn_subr-1',   'svn_pool_create'],
              ['svn_delta-1',  'svn_txdelta_next_window'],
              ['svn_client-1', 'svn_client_commit'],
              ['svn_wc-1',     'svn_wc_entry'],
              ['svn_ra-1',     'svn_ra_get_ra_library'],
]

libraries.each do |lib,func| 
  unless have_library(lib, func)
    puts "You seem to be missing the #{lib} library.\nI can't compile the "+
      "svn library without this." 
    exit(1)
  end
end

# These aren't required, but we'll link them if we have them
have_library('svn_fs-1', 'svn_fs_new')
have_library('svn_repos-1', 'svn_repos_open')

with_config('svn')

create_makefile('svn')
