# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================
#
# Script to build all the dependencies for Subversion on Windows
# It's been written for Windows 8 and Visual Studio 2012, but
# it's entirely possible it will work with older versions of both.

# The goal here is not to necessarily have everyone using this script.
# But rather to be able to produce binary packages of the dependencies
# already built to allow developers to be able to download or checkout
# Subversion and quickly get up a development environment.

# Prerequisites:
# Perl: http://www.activestate.com/activeperl/downloads
# Python: http://www.activestate.com/activepython/downloads
# 7-Zip: http://www.7-zip.org/download.html
# CMake: http://www.cmake.org/cmake/resources/software.html
# Microsoft Visual Studio 2012 (Ultimate has been tested, Express does not work)
#
# You probably want these on your PATH.  The installers usually
# offer an option to do that for you so if you can let them.
#
# You are expected to run this script within the correct Visual Studio
# Shell.  Probably "VS2012 x86 Native Tools Command Prompt".  This
# sets the proper PATH arguments so that the the compiler tools are
# available.
#
# TODO:
# Find some way to work around the lack of devenv in Express (msbuild will help some)
# Include a package target that zips everything up.
# Perl script that runs the Subversion get-make.py tool with the right args.
# Alternatively update gen-make.py with an arg that knows about our layout.
# Make the Windows build not expect to go looking into source code (httpd/zlib)
# Add SWIG (to support checkout builds where SWIG generation hasn't been done).
# Usage/help output from the usual flags/on error input.
# Make SQLITE_VER friendly since we're using no dots right now.
# Work out the fixes to the projects' sources and contribute them back.
# Allow selection of Arch (x86 and x64)
# ZLib support for OpenSSL (have to patch openssl)
# Use CMake zlib build instead.
# Assembler support for OpenSSL.
# Add more specific commands to the command line (e.g. build-httpd)

###################################
######   V A R I A B L E S   ######
###################################
package Vars;
# variables in the Vars package can be overriden from the command
# line with the FOO=BAR syntax.  If you want any defaults to reference
# other variables the defaults need to be in set_defaults() below to
# allow the defaults to be set after processing user set variables.

# Paths to commands to use, provide full paths if it's not
# on your PATH already.
our $SEVEN_ZIP = 'C:\Program Files\7-Zip\7z.exe';
our $CMAKE = 'cmake';
our $NMAKE = 'nmake';
# Use the .com version so we get output, the .exe doesn't produce any output
our $DEVENV = 'devenv.com';
our $VCUPGRADE = 'vcupgrade';
our $PYTHON = 'python';

# Versions of the dependencies we will use
# Change these if you want but these are known to work with
# this script as is.
our $HTTPD_VER = '2.4.4';
our $APR_VER = '1.4.6';
our $APU_VER = '1.5.2'; # apr-util version
our $API_VER = '1.2.1'; # arp-iconv version
our $ZLIB_VER = '1.2.8';
our $OPENSSL_VER = '1.0.1e';
our $PCRE_VER = '8.35';
our $BDB_VER = '5.3.21';
our $SQLITE_VER = '3071602';
our $SERF_VER = '1.3.6';
our $NEON_VER = '0.29.6';

# Sources for files to download
our $AWK_URL = 'http://www.cs.princeton.edu/~bwk/btl.mirror/awk95.exe';
our $HTTPD_URL;
our $APR_URL;
our $APU_URL;
our $API_URL;
our $ZLIB_URL;
our $OPENSSL_URL;
our $PCRE_URL;
our $BDB_URL;
our $SQLITE_URL;
our $SERF_URL;
our $NEON_URL;
our $PROJREF_URL = 'https://downloads.redhoundsoftware.com/blog/ProjRef.py';

# Location of the already downloaded file.
# by default these are undefined and set by the downloader.
# However, they can be overriden from the commandline and then
# the downloader is skipped.  Note that BDB has no downloader
# so it must be overriden from the command line.
our $AWK_FILE;
our $HTTPD_FILE;
our $APR_FILE;
our $APU_FILE;
our $API_FILE;
our $ZLIB_FILE;
our $OPENSSL_FILE;
our $PCRE_FILE;
our $BDB_FILE;
our $SQLITE_FILE;
our $SERF_FILE;
our $NEON_FILE;
our $PROJREF_FILE;

# Various directories we use
our $TOPDIR = Cwd::cwd(); # top of our tree
our $INSTDIR; # where we install to
our $BLDDIR; # directory where we actually build
our $SRCDIR; # directory where we store package files

# Some other options
our $VS_VER;
our $NEON;
our $SVN_VER = '1.9.x';
our $DEBUG = 0;

# Utility function to remove dots from a string
sub remove_dots {
  my $in = shift;

  $in =~ tr/.//d;
  return $in;
}

# unless the variable is already defined set the value
sub set_default {
  my $var = shift;
  my $value = shift;

  unless (defined($$var)) {
    $$var = $value;
  }
}

sub set_svn_ver_defaults {
  my ($svn_major, $svn_minor, $svn_patch) = $SVN_VER =~ /^(\d+)\.(\d+)\.(.+)$/;

  if ($svn_major > 1 or ($svn_major == 1 and $svn_minor >= 8)) {
    $NEON=0 unless defined($NEON);
  } else {
    $NEON=1 unless defined($NEON);
  }
}

# Any variables with defaults that reference other values
# should be set here.  This defers setting of the default until runtime in these cases.
sub set_defaults {
  set_default(\$HTTPD_URL, "http://archive.apache.org/dist/httpd/httpd-$HTTPD_VER.tar.bz2");
  set_default(\$APR_URL, "http://archive.apache.org/dist/apr/apr-$APR_VER.tar.bz2");
  set_default(\$APU_URL, "http://archive.apache.org/dist/apr/apr-util-$APU_VER.tar.bz2");
  set_default(\$API_URL, "http://archive.apache.org/dist/apr/apr-iconv-$API_VER.tar.bz2");
  set_default(\$ZLIB_URL, "http://sourceforge.net/projects/libpng/files/zlib/$ZLIB_VER/zlib" . remove_dots($ZLIB_VER) . '.zip');
  set_default(\$OPENSSL_URL, "http://www.openssl.org/source/openssl-$OPENSSL_VER.tar.gz");
  set_default(\$PCRE_URL, "ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-$PCRE_VER.zip");
  set_default(\$BDB_URL, "http://download.oracle.com/berkeley-db/db-5.3.21.zip");
  set_default(\$SQLITE_URL, "http://www.sqlite.org/2013/sqlite-amalgamation-$SQLITE_VER.zip");
  set_default(\$SERF_URL, "https://archive.apache.org/dist/serf/serf-$SERF_VER.zip");
  set_default(\$NEON_URL, "http://www.webdav.org/neon/neon-$NEON_VER.tar.gz");
  set_default(\$INSTDIR, $TOPDIR);
  set_default(\$BLDDIR, "$TOPDIR\\build");
  set_default(\$SRCDIR, "$TOPDIR\\sources");
  set_svn_ver_defaults();
}

#################################
######       M A I N       ######
#################################
# You shouldn't have any reason to modify below this unless you've changed
# versions of something.
package main;

use warnings;
use strict;

use LWP::Simple;
use File::Path;
use File::Copy;
use File::Basename;
use File::Find;
use Cwd;
use Config;

# Full path to perl, this shouldn't need to be messed with
my $PERL = $Config{perlpath};

# Directory constants that we setup for convenience, but that
# shouldn't be changed since they are assumed in the build systems
# of the various dependencies.
my $HTTPD; # Where httpd gets built
my $BDB; # Where bdb gets built
my $BINDIR; # where binaries are installed
my $LIBDIR; # where libraries are installed
my $INCDIR; # where headers are installed
my $SRCLIB; # httpd's srclib dir

# defer setting these values till runtime so users can override the
# user controlled vars they derive from.
sub set_paths {
  $HTTPD = "$BLDDIR\\httpd";
  $BDB = "$BLDDIR\\bdb";
  $BINDIR = "$INSTDIR\\bin";
  $LIBDIR = "$INSTDIR\\lib";
  $INCDIR = "$INSTDIR\\include";
  $SRCLIB = "$HTTPD\\srclib";
  # Add bin to PATH this will be needed for at least awk later on
  $ENV{PATH} = "$BINDIR;$ENV{PATH}";
  # Setup LIB and INCLUDE so we can find BDB
  $ENV{LIB} = "$LIBDIR;$ENV{LIB}";
  $ENV{INCLUDE} = "$INCDIR;$ENV{INCLUDE}";
}

#####################
# UTILTIY FUNCTIONS #
#####################

# copy a file with error handling
sub copy_or_die {
  my $src = shift;
  my $dest = shift;

  copy($src, $dest) or die "Failed to copy $src to $dest: $!";
}

# Rename a file and deal with errors.
sub rename_or_die {
  my $src = shift;
  my $dest = shift;

  rename($src, $dest) or die "Failed to rename $src to $dest: $!";
}

# Utility function to chdir with error handling.
sub chdir_or_die {
  my $dir = shift;

  chdir($dir) or die "Failed to chdir to $dir: $!";
}

# Utility function to call system with error handling.
# First arg is an error message to print if something fails.
# Remaining args are passed to system.
sub system_or_die {
  my $error_msg = shift;
  unless (system(@_) == 0) {
    if (defined($error_msg)) {
      die "$error_msg (exit code: $?)";
    } else {
      die "Failed while running '@_' (exit code: $?)";
    }
  }
}

# Like perl -pi.orig the second arg is a reference to a
# function that does whatever line processing you want.
# Note that $_ is used for the input and output of the
# function.  So modifying $_ changes the line in the file.
# bak can be passed to set the backup extension.  If the
# backup file already exists, shortcut this step.
sub modify_file_in_place {
  my $file = shift;
  my $func = shift;
  my $bak = shift;

  unless (defined($bak)) {
    $bak = '.orig';
  }

  my $backup = $file . $bak;
  return if -e $backup;
  rename_or_die($file, $backup);
  open(IN, "<$backup") or die "Failed to open $backup: $!";
  open(OUT, ">$file") or die "Failed to open $file: $!";
  while (<IN>) {
    &{$func}();
    print OUT;
  }
  close(IN);
  close(OUT);
}

sub check_vs_ver {
  return if defined($VS_VER);

  # using the vcupgrade command here because it has a consistent name and version
  # numbering across versions including express versions.
  my $help_output = `"$VCUPGRADE" /?`;
  my ($major_version) = $help_output =~ /Version (\d+)\./s;

  if (defined($major_version)) {
    if ($major_version eq '12') {
      $VS_VER = '2013';
      return;
    } elsif ($major_version eq '11') {
      $VS_VER = '2012';
      return;
    } elsif ($major_version eq '10') {
      $VS_VER = '2010';
      return;
    }
  }

  die("Visual Studio Version Not Supported");
}

##################
# TREE STRUCTURE #
##################

# Create directories that this script directly needs
sub prepare_structure {
  # ignore errors the directories may already exist.
  mkdir($BINDIR);
  mkdir($SRCDIR);
  mkdir($BLDDIR);
  mkdir($LIBDIR);
  mkdir($INCDIR);
}

# Remove paths created by this script (directly or indecirectly)
# If the first arg is 1 it'll remove the downloaded files otherwise it
# leaves them alone.
sub clean_structure {
  # ignore errors in this function the paths may not exist
  my $real_clean = shift;

  if ($real_clean) {
    rmtree($SRCDIR);
  }
  rmtree($BINDIR);
  rmtree($BLDDIR);
  rmtree($INCDIR);
  rmtree($LIBDIR);
  rmtree("$INSTDIR\\serf");
  rmtree("$INSTDIR\\neon");
  rmtree("$INSTDIR\\sqlite-amalgamation");

  # Dirs created indirectly by the install targets
  rmtree("$INSTDIR\\man");
  rmtree("$INSTDIR\\share");
  rmtree("$INSTDIR\\ssl");
  rmtree("$INSTDIR\\cgi-bin");
  rmtree("$INSTDIR\\conf");
  rmtree("$INSTDIR\\error");
  rmtree("$INSTDIR\\htdocs");
  rmtree("$INSTDIR\\icons");
  rmtree("$INSTDIR\\logs");
  rmtree("$INSTDIR\\manual");
  rmtree("$INSTDIR\\modules");
  unlink("$INSTDIR\\ABOUT_APACHE.txt");
  unlink("$INSTDIR\\CHANGES.txt");
  unlink("$INSTDIR\\INSTALL.txt");
  unlink("$INSTDIR\\LICENSE.txt");
  unlink("$INSTDIR\\NOTICE.txt");
  unlink("$INSTDIR\\OPENSSL-NEWS.txt");
  unlink("$INSTDIR\\OPENSSL-README.txt");
  unlink("$INSTDIR\\README.txt");
}

############
# DOWNLOAD #
############

# Download a url into a file if successful put the destination into the
# variable referenced by $dest_ref.
sub download_file {
  my $url = shift;
  my $file = shift;
  my $dest_ref = shift;

  # If the variable referenced by $dest_ref is already set, skip downloading
  # means we've been asked to use an already downloaded file.
  return if (defined($$dest_ref));

  print "Downloading $url\n";
  # Using mirror() here so that repeated runs shouldn't try to keep downloading
  # the file.
  my $response = mirror($url, $file);
  if (is_error($response)) {
    die "Couldn't save $url to $file received $response";
  }
  $$dest_ref = $file;
}

# Download all the dependencies we need
sub download_dependencies {
  # putting awk in sources is a bit of a hack but it lets us
  # avoid having to figure out what to delete when cleaning bin
  download_file($AWK_URL, "$SRCDIR\\awk.exe", \$AWK_FILE);
  unless(-x "$BINDIR\\awk.exe") { # skip the copy if it exists
    copy_or_die($AWK_FILE, "$BINDIR\\awk.exe");
  }
  download_file($PROJREF_URL, "$SRCDIR\\ProjRef.py", \$PROJREF_FILE);
  unless(-x "$BINDIR\\ProjRef.py") { # skip the copy if it exists
    copy_or_die($PROJREF_FILE, $BINDIR);
  }
  download_file($BDB_URL, "$SRCDIR\\db.zip", \$BDB_FILE);
  download_file($ZLIB_URL, "$SRCDIR\\zlib.zip", \$ZLIB_FILE);
  download_file($OPENSSL_URL, "$SRCDIR\\openssl.tar.gz", \$OPENSSL_FILE);
  download_file($HTTPD_URL, "$SRCDIR\\httpd.tar.bz2", \$HTTPD_FILE);
  download_file($APR_URL, "$SRCDIR\\apr.tar.bz2", \$APR_FILE);
  download_file($APU_URL, "$SRCDIR\\apr-util.tar.bz2", \$APU_FILE);
  download_file($API_URL, "$SRCDIR\\apr-iconv.tar.bz2", \$API_FILE);
  download_file($PCRE_URL, "$SRCDIR\\pcre.zip", \$PCRE_FILE);
  download_file($SQLITE_URL, "$SRCDIR\\sqlite-amalgamation.zip", \$SQLITE_FILE);
  download_file($SERF_URL, "$SRCDIR\\serf.zip", \$SERF_FILE);
  download_file($NEON_URL, "$SRCDIR\\neon.tar.gz", \$NEON_FILE) if $NEON;
}

##############
# EXTRACTION #
##############

# Extract a compressed file with 7-zip into a given directory
# Skip extraction if destination of rename_to or expected_name exists
# if rename_to is set rename the path from expected_name to rename_to
sub extract_file {
  my $file = shift;
  my $container = shift;
  my $expected_name = shift;
  my $rename_to = shift;

  if (defined($rename_to)) {
    return if -d $rename_to;
  } elsif (defined($expected_name)) {
    return if -d $expected_name;
  }

  my $dest_opt = "";
  if (defined($container)) {
    $dest_opt = qq(-o"$container" );
  }

  my $cmd;
  if ($file =~ /\.tar\.(bz2|gz)$/) {
    $cmd = qq("$SEVEN_ZIP" x "$file" -so | "$SEVEN_ZIP" x -y -si -ttar $dest_opt);
  } else {
    $cmd = qq("$SEVEN_ZIP" x -y $dest_opt $file);
  }

  system_or_die("Problem extracting $file", $cmd);
  if (defined($rename_to)) {
    rename_or_die($expected_name, $rename_to);
  }
}

sub extract_dependencies {
  extract_file($BDB_FILE, $BLDDIR,
               "$BLDDIR\\db-$BDB_VER", "$BLDDIR\\bdb");
  extract_file($HTTPD_FILE, $BLDDIR,
               "$BLDDIR\\httpd-$HTTPD_VER", "$BLDDIR\\httpd");
  extract_file($APR_FILE, $SRCLIB,
               "$SRCLIB\\apr-$APR_VER", "$SRCLIB\\apr");
  extract_file($APU_FILE, $SRCLIB,
               "$SRCLIB\\apr-util-$APU_VER", "$SRCLIB\\apr-util");
  extract_file($API_FILE, $SRCLIB,
               "$SRCLIB\\apr-iconv-$API_VER", "$SRCLIB\\apr-iconv");
  # We fix the line endings before putting the non-Apache deps in place since it
  # touches everything under httpd and there's no point in doing other things.
  httpd_fix_lineends();
  extract_file($ZLIB_FILE, $SRCLIB,
               "$SRCLIB\\zlib-$ZLIB_VER", "$SRCLIB\\zlib");
  extract_file($OPENSSL_FILE, $SRCLIB,
               "$SRCLIB\\openssl-$OPENSSL_VER", "$SRCLIB\\openssl");
  extract_file($PCRE_FILE, $SRCLIB,
               "$SRCLIB\\pcre-$PCRE_VER", "$SRCLIB\\pcre");
  extract_file($SQLITE_FILE, $INSTDIR,
               "$INSTDIR\\sqlite-amalgamation-$SQLITE_VER",
               "$INSTDIR\\sqlite-amalgamation");
  extract_file($SERF_FILE, $INSTDIR,
               "$INSTDIR\\serf-$SERF_VER", "$INSTDIR\\serf");
  extract_file($NEON_FILE, $INSTDIR,
               "$INSTDIR\\neon-$NEON_VER", "$INSTDIR\\neon") if $NEON;
}

#########
# BUILD #
#########

sub build_pcre {
  chdir_or_die("$SRCLIB\\pcre");
  my $pcre_generator = 'NMake Makefiles';
  # Have to use RelWithDebInfo since httpd looks for the pdb files
  my $pcre_build_type = '-DCMAKE_BUILD_TYPE:STRING=' . ($DEBUG ? 'Debug' : 'RelWithDebInfo');
  my $pcre_options = '-DPCRE_NO_RECURSE:BOOL=ON';
  my $pcre_shared_libs = '-DBUILD_SHARED_LIBS:BOOL=ON';
  my $pcre_install_prefix = "-DCMAKE_INSTALL_PREFIX:PATH=$INSTDIR";
  my $cmake_cmd = qq("$CMAKE" -G "$pcre_generator" "$pcre_build_type" "$pcre_shared_libs" "$pcre_install_prefix" "$pcre_options" .);
  system_or_die("Failure generating pcre Makefiles", $cmake_cmd);
  system_or_die("Failure building pcre", qq("$NMAKE"));
  system_or_die("Failure testing pcre", qq("$NMAKE" test));
  system_or_die("Failure installing pcre", qq("$NMAKE" install));
  chdir_or_die($TOPDIR);
}

# This is based roughly off the build_zlib.bat that the Subversion Windows
# build generates, it it doesn't match that then Subversion will fail to build.
sub build_zlib {
  chdir_or_die("$SRCLIB\\zlib");
  $ENV{CC_OPTS} = $DEBUG ? '/MDd /Gm /ZI /Od /GZ /D_DEBUG' : '/MD /02 /Zi';
  $ENV{COMMON_CC_OPTS} = '/nologo /W3 /DWIN32 /D_WINDOWS';

  system_or_die("Failure building zilb", qq("$NMAKE" /nologo -f win32\\Makefile.msc STATICLIB=zlibstat.lib all));

  delete $ENV{CC_OPTS};
  delete $ENV{COMMON_CC_OPTS};

  chdir_or_die($TOPDIR);
}

sub build_openssl {
  chdir_or_die("$SRCLIB\\openssl");

  # We're building openssl without an assembler.  If someone wants to
  # use this for production they should probably download NASM and
  # remove the no-asm below and use ms\do_nasm.bat instead.

  # TODO: Enable openssl to use zlib.  openssl needs some patching to do
  # this since it wants to look for zlib as zlib1.dll and as the httpd
  # build instructions note you probably don't want to dynamic link zlib.

  # TODO: OpenSSL requires perl on the path since it uses perl without a full
  # path in the batch file and the makefiles.  Probably should determine
  # if PERL is on the path and add it here if not.

  # The apache build docs suggest no-rc5 no-idea enable-mdc2 on top of what
  # is used below, the primary driver behind that is patents, but I believe
  # the rc5 and idea patents have expired.
  my $platform = $DEBUG ? 'debug-VC-WIN32' : 'VC-WIN32';
  system_or_die("Failure configuring openssl",
                qq("$PERL" Configure no-asm "--prefix=$INSTDIR" $platform));
  system_or_die("Failure building openssl (bat)", 'ms\do_ms.bat');
  system_or_die("Failure building openssl (nmake)", qq("$NMAKE" /f ms\\ntdll.mak));
  system_or_die("Failure testing openssl", qq("$NMAKE" /f ms\\ntdll.mak test));
  system_or_die("Failure installing openssl",
                qq("$NMAKE" /f ms\\ntdll.mak install));
  chdir_or_die($TOPDIR);
}

# Run devenv /Upgrade on file.
# If the file isn't a .sln file and the sln file isn't empty shortcut this
# If the file isn't a .sln file touch the basename.sln of file to avoid
# Visual Studio whining about its backup step.
sub upgrade_solution {
  my $file = shift;
  my $interactive = shift;
  my $flags = "";

  my ($basename, $directories) = fileparse($file, qr/\.[^.]*$/);
  my $sln = $directories . $basename . '.sln';
  return if $file ne $sln and -s $sln; # shortcut if sln file is unique and isn't empty
  # 'touch' the sln file so that Visual Studio 2012
  # doesn't try to say there was an error while upgrading because
  # it was unable to backup the original solution file.
  unless (-e $sln) {
    open(SLN, ">$sln") or die "Can't create $sln: $!";
    close(SLN);
  }
  print "Upgrading $file (this may take a while)\n";
  $flags = " /Upgrade" unless $interactive;
  system_or_die("Failure upgrading $file", qq("$DEVENV" "$file"$flags));
  if ($interactive) {
    print "Can't do automatic upgrade, doing interactive upgrade\n";
    print "IDE will load, choose to convert all projects, exit the IDE and\n";
    print "save the resulting solution file\n\n";
    print "Press Enter to Continue\n";
    <>;
  }
}

# Run the lineends.pl script
sub httpd_fix_lineends {
  chdir_or_die($HTTPD);
  # This script fixes the lineendings to be CRLF in appropriate files.
  # If we don't run this script then the DSW Upgrade will fail.
  system_or_die(undef, qq("$PERL" "$SRCLIB\\apr\\build\\lineends.pl"));
  chdir_or_die($TOPDIR);
}

# The httpd makefile in 2.4.4 doesn't know about .vcxproj files and
# still thinks it's got an older version of Visual Studio because
# .vcproj files have become .vcxproj.
sub httpd_fix_makefile {
  my $file = shift;

  modify_file_in_place($file, sub {
      s/\.vcproj/.vcxproj/i;
      # below fixes that installd breaks when trying to install pcre because
      # dll is named pcred.dll when a Debug build.
      s/^(\s*copy srclib\\pcre\\pcre\.\$\(src_dll\)\s+"\$\(inst_dll\)"\s+<\s*\.y\s*)$/!IF EXISTS("srclib\\pcre\\pcre\.\$(src_dll)")\n$1!ENDIF\n!IF EXISTS("srclib\\pcre\\pcred\.\$(src_dll)")\n\tcopy srclib\\pcre\\pcred.\$(src_dll)\t\t\t"\$(inst_dll)" <.y\n!ENDIF\n/;
    });
}

# This is a poor mans way of inserting a property group into a
# vcxproj file.  It assumes that the ending Project tag will
# be the start and end of the line with no whitespace, probably
# not an entirely valid assumption but it works in this case.
sub insert_property_group {
  my $file = shift;
  my $xml = shift;
  my $bak = shift;

  modify_file_in_place($file, sub {
      s#(^</Project>$)#<PropertyGroup>$xml</PropertyGroup>\n$1#i;
    }, $bak);
}

# Strip pre-compiled headers compile and linker flags from file they follow
# the form: /Ycfoo.h or /Yufoo.h.
sub disable_pch {
  my $file = shift;

  modify_file_in_place($file, sub {
      s#/Y[cu][^ ]+##;
    });
}

# Find the first .exe .dll or .so OutputFile in the project
# provided by file.  There may be macros or paths in the
# result.
sub get_output_file {
  my $file = shift;
  my $result;
  local $_; # Don't mess with the $_ from the find callback

  open(IN, "<$file") or die "Couldn't open file $file: $!";
  while (<IN>) {
    if (m#<OutputFile>(.*?\.(?:exec|dll|so))</OutputFile>#) {
      $result = $1;
      last;
    }
  }
  close(IN);
  return $result;
}

# Find the name of the bdb library we've installed in our LIBDIR.
sub find_bdb_lib {
  my $result;
  my $debug = $DEBUG ? 'd' : '';
  find(sub {
         if (not defined($result) and /^libdb\d+$debug\.lib$/) {
           $result = $_;
         }
       }, $LIBDIR);
  return $result;
}

# Insert the dependency dep into project file.
# bak can be set to set the backup filename made of the project.
sub insert_dependency_in_proj {
  my $file = shift;
  my $dep = shift;
  my $bak = shift;

  modify_file_in_place($file, sub {
      s/(%\(AdditionalDependencies\))/$dep;$1/;
    }, $bak);
}

# Do what's needed to enable BDB in the httpd and apr-util builds
sub httpd_enable_bdb {
  # Make APU_HAVE_DB be true so the code builds.
  modify_file_in_place('srclib\apr-util\include\apu.hw', sub {
      s/(#define\s+APU_HAVE_DB\s+)0/${1}1/;
    });

  # Fix the linkage, apr_dbm_db is hardcoded to libdb47.lib
  my $bdb_lib = find_bdb_lib();
  modify_file_in_place('srclib\apr-util\dbm\apr_dbm_db.vcxproj', sub {
      s/libdb\d+\.lib/$bdb_lib/g;
    }, '.bdb');

  # httxt2dbm and htdbm need a BDB dependency and don't have one.
  insert_dependency_in_proj('support\httxt2dbm.vcxproj', $bdb_lib, '.bdb');
  insert_dependency_in_proj('support\htdbm.vcxproj', $bdb_lib, '.bdb');
}

# Apply the same fix as found in r1486937 on httpd 2.4.x branch.
sub httpd_fix_debug {
  my ($httpd_major, $httpd_minor, $httpd_patch) = $HTTPD_VER =~ /^(\d+)\.(\d+)\.(.+)$/;
  return unless ($httpd_major <= 2 && $httpd_minor <= 4 && $httpd_patch < 5);

  modify_file_in_place('libhttpd.dsp', sub {
      s/^(!MESSAGE "libhttpd - Win32 Debug" \(based on "Win32 \(x86\) Dynamic-Link Library"\))$/$1\n!MESSAGE "libhttpd - Win32 Lexical" (based on "Win32 (x86) Dynamic-Link Library")/;
      s/^(# Begin Group "headers")$/# Name "libhttpd - Win32 Lexical"\n$1/;
    }, '.lexical');
}

sub build_httpd {
  chdir_or_die($HTTPD);

  my $vs_2013 = $VS_VER eq '2013';
  my $vs_2012 = $VS_VER eq '2012';
  my $vs_2010 = $VS_VER eq '2010';

  httpd_fix_debug();

  # I don't think cvtdsp.pl is necessary with Visual Studio 2012
  # but it shouldn't hurt anything either.  Including it allows
  # for the possibility that this may work for older Visual Studio
  # versions.
  system_or_die("Failure converting DSP files",
                qq("$PERL" srclib\\apr\\build\\cvtdsp.pl -2005));

  upgrade_solution('Apache.dsw', $vs_2010);
  httpd_enable_bdb();
  httpd_fix_makefile('Makefile.win');

  # Modules and support projects randomly fail due to an error about the
  # CL.read.1.tlog file already existing.  This is really because of the
  # intermediate dirs being shared between modules, but for the time being
  # this works around it.
  find(sub {
         if (/\.vcxproj$/) {
           insert_property_group($_, '<TrackFileAccess>false</TrackFileAccess>')
         }
       }, 'modules', 'support');

  if ($vs_2012 or $vs_2013) {
    # Turn off pre-compiled headers for apr-iconv to avoid:
    # LNK2011: http://msdn.microsoft.com/en-us/library/3ay26wa2(v=vs.110).aspx
    disable_pch('srclib\apr-iconv\build\modules.mk.win');

    # ApacheMonitor build fails due a duplicate manifest, turn off
    # GenerateManifest
    insert_property_group('support\win32\ApacheMonitor.vcxproj',
                          '<GenerateManifest>false</GenerateManifest>',
                          '.dupman');

    # The APR libraries have projects named libapr but produce output named libapr-1
    # The problem with this is in newer versions of Visual Studio TargetName defaults
    # to the project name and not the basename of the output.  Since the PDB file
    # is named based on the TargetName the pdb file ends up being named libapr.pdb
    # instead of libapr-1.pdb.  The below call fixes this by explicitly providing
    # a TargetName definition and shuts up some warnings about this problem as well.
    # Without this fix the install fails when it tries to copy libapr-1.pdb.
    # See this thread for details of the changes:
    # http://social.msdn.microsoft.com/Forums/en-US/vcprerelease/thread/3c03e730-6a0e-4ee4-a0d6-6a5c3ce4343c
    find(sub {
           return unless (/\.vcxproj$/);
           my $output_file = get_output_file($_);
           return unless (defined($output_file));
           my ($project_name) = fileparse($_, qr/\.[^.]*$/);
           my ($old_style_target_name) = fileparse($output_file, qr/\.[^.]*$/);
           return if ($old_style_target_name eq $project_name);
           insert_property_group($_,
             "<TargetName>$old_style_target_name</TargetName>", '.torig');
         }, "$SRCLIB\\apr", "$SRCLIB\\apr-util", "$SRCLIB\\apr-iconv");
  } elsif ($vs_2010) {
    system_or_die("Failed fixing project guid references",
      qq("$PYTHON" "$BINDIR\\ProjRef.py" -i Apache.sln"));
  }

  # If you're looking here it's possible that something went
  # wrong with the httpd build.  Debugging it can be a bit of a pain
  # when using this script.  There are log files created in the
  # Release dirs named with the same basename as the project.  E.G.
  # for support\httxt2dbm.vcxproj you can find the log in
  # support\Release\httxt2dbm.log.  You can also run a similar build
  # from in the IDE, but you'll need to disable some projects since
  # they are separately driven by the Makefile.win.  Grepping for
  # '/project' in Makefile.win should tell you which projects.  You'll
  # also need to add the bin, include and lib paths to the appropriate
  # configurations inside the project since we get them from the environment.
  # Once all that is done the BuildBin project should be buildable for you to
  # diagnose the problem.
  my $target = $DEBUG ? "installd" : "installr";
  system_or_die("Failed building/installing httpd/apr/apu/api",
    qq("$NMAKE" /f Makefile.win $target "DBM_LIST=db" "INSTDIR=$INSTDIR"));

  chdir_or_die($TOPDIR);
}

sub build_bdb {
  chdir_or_die($BDB);

   print(cwd(),$/);
  my $sln = 'build_windows\Berkeley_DB_vs2010.sln';
  upgrade_solution($sln);

  my $platform = $DEBUG ? 'Debug|Win32' : 'Release|Win32';

  # Build the db Project first since the full solution fails due to a broken
  # dependency with the current version of BDB if we don't.
  system_or_die("Failed building DBD (Project db)",
                qq("$DEVENV" "$sln" /Build "$platform" /Project db));

  system_or_die("Failed building DBD",
                qq("$DEVENV" "$sln" /Build "$platform"));

  # BDB doesn't seem to have it's own install routines so we'll do it ourselves
  copy_or_die('build_windows\db.h', $INCDIR);
  find(sub {
     if (/\.(exe|dll|pdb)$/) {
       copy_or_die($_, $BINDIR);
     } elsif (/\.lib$/) {
       copy_or_die($_, $LIBDIR);
     }
   }, 'build_windows\\Win32\\' . ($DEBUG ? 'Debug' : 'Release'));

  chdir_or_die($TOPDIR);
}

# Right now this doesn't actually build serf but just patches it so that it
# can build against a debug build of OpenSSL.
sub build_serf {
  chdir_or_die("$TOPDIR\\serf");

  modify_file_in_place('serf.mak', sub {
      s/^(INTDIR = Release)$/$1\nOPENSSL_OUT_SUFFIX =/;
      s/^(INTDIR = Debug)$/$1\nOPENSSL_OUT_SUFFIX = .dbg/;
      s/(\$\(OPENSSL_SRC\)\\out32(?:dll)?)/$1\$(OPENSSL_OUT_SUFFIX)/g;
    }, '.debug');

  chdir_or_die($TOPDIR);
}

sub build_dependencies {
  build_bdb();
  build_zlib();
  build_pcre();
  build_openssl();
  build_serf();
  build_httpd();
}

###############
# COMMANDLINE #
###############

# Implement an interface somewhat similar to the make command line
# You can give a list of commands and variable assignments interspersed.
# Variable assignments are always VAR=VALUE with no spaces (in a single
# argv entry).
sub main {
  my @commands;
  while (my $arg = shift @ARGV) {
    # Look for variable assignment
    if (my ($lhs, $rhs) = $arg =~ /([^=]+)=(.*)/) {
      # Bit of hackery to allow the global values in the
      # Vars package to be overriden from the command line.
      # E.G. "CMAKE=C:\CMake\cmake.exe" would replace the
      # default value with this value.
      if (exists($Vars::{$lhs})) {
        ${$Vars::{$lhs}} = $rhs;
      } else {
        # Don't allow variables that don't exist already to be touched.
        die "$lhs is an unknown variable.";
      }
    } else {
      # Not a variable so must be a command
      push @commands, $arg;
    }
  }

  # No commands so add the implicit all command
  if ($#commands == -1) {
    push @commands, 'all';
  }

  # Set defaults and paths that have to be set at runtime since they are based
  # on other variables.
  Vars::set_defaults();
  set_paths();

  # Determine the Visual Studio Version and die if not supported.
  check_vs_ver();

  # change directory to our TOPDIR before running any commands
  # the variable assignment might have changed it.
  chdir_or_die($TOPDIR);

  # Run the commands in the order given.
  foreach my $command (@commands) {
    if ($command eq 'clean') {
      clean_structure(0);
    } elsif ($command eq 'real-clean') {
      clean_structure(1);
    } elsif ($command eq 'prepare') {
      prepare_structure();
    } elsif ($command eq 'download') {
      download_dependencies();
    } elsif ($command eq 'extract') {
      extract_dependencies();
    } elsif ($command eq 'all') {
      prepare_structure();
      download_dependencies();
      extract_dependencies();
      build_dependencies();
    } else {
      die "Command '$command' is unknown";
    }
  }
}

main();
