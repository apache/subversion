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

# Extra libraries needed to compile
libraries = %w{svn_subr svn_delta svn_client svn_wc svn_ra}
libraries.each do |lib| 
  unless have_library(lib, nil)
    puts "You seem to be missing the #{lib} library.\nI can't compile the "+
      "svn library without this." 
    exit(1)
  end
end
# These aren't required, but we'll link them if we have them
have_library('svn_fs')
have_library('svn_repos')

$LDFLAGS << `apr-config --ldflags`.chop
$LOCAL_LIBS << `apr-config --libs`.chop
$CFLAGS << `apr-config --cflags`.chop

# Linux needs -lpthread.
if PLATFORM =~ /linux/ && $CFLAGS =~ /-pthread/ then
  have_library('pthread')
end

with_config('svn')

create_makefile('svn')
