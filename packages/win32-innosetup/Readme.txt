Information about the installation program for Subversion for Windows
=====================================================================

$LastChangedDate$

CONTENTS:
=========
  * Introduction (read first!)
  * Directory structure
  * Programs used for the Subversion installer and instructions
    - Inno Setup
    - ISTool
    - 7-zip
    - GNU diffutils (Cygwin)
    - Perl
    - Packages for converting XML documentation
    - MS HTML Help Workshop
  * Making a distro


Introduction
============

  This document describes the packages\win32-innosetup directory of the
  Subversion repository and tells you how you can roll out your own
  Windows installer for Subversion.
  
  If you haven't done it already:  -Please, Check out the subversion
  sources to a place you like and download the programs and packages
  from the links below. Reading the "Directory structure" part should
  be the next.


  Inno Setup
  ----------
  Inno Setup (IS) 3.0.6 or better with "My Inno Setup Extentions"
  (ISX) and "Inno Setup Pre Processor" (ISPP) included:
    http://www.wintax.nl/isx/

  ISTool
  ------
  ISTool 3.0.6.1 or better:
    http://www.bhenden.org/istool/

  7-zip
  -----
  7-Zip 2.30 Beta 26 or better:
    http://www.7-zip.org/

  GNU diffutils
  -------------
  Get Cygwin with diffutils-2.8.1 or better from:
    http://www.cygwin.com/

  Perl
  ----
  Perl 5.8.0 or better with the libwin32 bundle (included in
  ActivePerl):
  	http://www.activestate.com/ActivePerl/
  or make your own (and get the module libwin32):
    http://www.perl.com/
    http://www.cpan.org/modules/by-module/Win32/ (libwin32-X.XXX.zip)

  libxml
  ------
  Point your browser to:
    http://www.fh-frankfurt.de/~igor/projects/libxml/index.html
  and grab the following packages:
    - libxml
    - libxslt
    - iconv
    - xsldbg (if you want to write XML docs).

  docbook-xsl
  -----------
  Point your browser to:
     http://sourceforge.net/project/showfiles.php?group_id=21935
  and grab the most recent docbook-xsl-*.**.*.zip file

  MS HTML Help Workshop
  ---------------------
  Point your browser to:
    http://msdn.microsoft.com/library/tools/htmlhelp/chm/hh1start.htm


  Read in the section named "Programs used for the Subversion Windows
  installer" below about the packages for notes and info on installing
  and using the downloaded packages.

Directory structure
===================

  The Inno setup file lives under the packages\win32-innosetup directory
  of the Subversion repository and are using folders which are both visible
  and "hidden". The hidden folders have the svn property svn:ignore and only
  exists on your machine.

  The setup system gets its files (and have files) from two kinds of places:
  * Static:  This files are allways somwhere in the reposistry.
  * Dynamic: This files can be picked up anywhere from your computer (even from
             the repository). All the paths here are determined by the file
             paths_inno_src.iss wich is variables that is proccessed by Inno
             Setup Pre Proccessor (ISPP) during the compiling of the setup.
             A template of this file can be found in the 
             packages\win32-innosetup\templates directory. Copy this file to
             the packages\win32-innosetup directory and edit it according to
             the documentation inside it. This file is not under version
             control (the template is) since the contents will vary from user
             to user.

  Visible folders looks like this: [ ] and hidden folders like this: [h].
  
  Do you think that is looks complicated? -Dont worry! The programs in the
  tools folder takes care of copying and preparing files when your 
  paths_inno_src.iss file are edited and set correctly.

  Static paths (in the Subversion repository):
  -------------  
  [ ] win32-innosetup                     (svn.iss, main folder for Inno Setup)
   +->[ ] images                             (Various images used by the setup)
   +->[h] in    (you can set your path_setup_in here if you want to, see below)
   +->[h] out  (you can set your path_setup_out here if you want to, see below)
   +->[ ] templates        (misc templates used by various tools and the setup)
   +->[ ] tools            (misc. stuff for making and helping to make a setup)
   |   +->[ ] svnpath                   (C sources for the svnpath.exe program)

  Dynamic paths (files from anywhere on your machine)
  ---------------------------------------------------

  This paths are detemined by values in the file paths_inno_src.iss. The value
  names of this path variabless is:
  
  Path variables:     Setup files:
  ---------------     ---------------------------------------------------------
  path_setup_out      Where svn-X.XX.X-rXXXX-setup.exe is to find after
                      compiling the setup
  path_setup_in       Contains misc. files to include in the setup
  path_isx            Path to ISX
  path_svnclient      svn.exe
  path_svnadmin       svnadmin.exe
  path_svnlook        svnlook.exe
  path_davsvn         mod_dav_svn.so
  path_svnpath        svnpath.exe
  path_brkdb_bin      db_*.exe, ex_*.exe, excxx_*.exe, libdb4*.dll, libdb4*.exp
  path_brkdb_lib      libdb4*.lib
  path_brkdb_inc      db.h, db_cxx.h)
  path_brkdb_inc2     cxx_common.h, cxx_except.h
  path_ssl            libeay32.dll, ssleay32.dll
  path_diffutls_bin   cygintl-1.dll, cygwin1.dll, diff.exe, diff3.exe



Programs used for the Subversion Windows installer
==================================================

  Inno Setup
  ----------
  The installation program is the exellent Inno Setup made by Jordan Russell.
  IS and friends are probably all you need for 99% of any Windows installer
  needs made for the various flavors of Windows and has proven to be extremely
  reliable and stable.

  The Inno Setup used by Subversion are extended with "My Inno Setup
  Extentions" made by Martijn Laan and "Inno Setup Pre Proccessor"
  made by Alex Yackimoff. ISX includes IS and ISPP so all you need to
  do, is to install "My Inno Setup Extentions"

  Oh, you should visit Jordan Russell's (the Inno Setup creater) homepage at:
    http://www.jrsoftware.org/
  He are doing more than Inno Setup and you can get lot of info from his
  homepage.

  Installation notes: None

  ISTool
  ------
  A good installation script for any installation programs are
  usually very complicated and requires good script editing software.
  The program used for this is ISTool and it's syntax high-lightning
  makes it the perfect companion to IS and friends.
  
  The author - Bjørnar Henden are doing a great job by updating his
  program each time Inno Setup are updated.
  It's also includes full support for "My Inno Setup Extentions" (see
  above).

  Installation notes: None

  7-zip
  -----
  7-zip offers a very high compression ratio. Usually, one use the
  compression Inno offers (gzip or bzip2) but 7-zip are compressing
  about 25% better than bzip2!
  In practice this means that the Inno Setup installation itself are
  uncompressed and a 7-zip SFX archive are triggering the Inno Setup
  installation after extracting the SFX.

  Installation notes: None

  svnpath
  -------
  Inno Setup does not currently edit the systems PATH environment so
  we need svnpath.
  This C program are used for updating the user's path to include/
  exclude the Subversion path after installing/un-installing
  Subversion.
  You can find the sources for this program in the Subversion source
  tree under packages\win32-innosetup\tools\svnpath
  
  Have a look in the file main.c for info on how to compile the
  program.
 
  libxml, libxslt and iconv
  -------------------------
  We need to include some documentation and this tools will help us to convert
  the XML files in the doc directory in the repository to a Windows HTML help
  file.
  
  Installation notes:
    Unpack the zip-files and place the contents of the 'lib' and 'util' folders
    from each unzipped packages in a folder which is mentioned in your PATH
    environment variable.
    
  docbook-xsl
  -----------
  This package is needed for making documentation.
  
  Unzip the files inside docbook-xsl-*.**.*.zip to a folder named xsl which
  resides under doc\book\tools in your working copy of the subversion
  repository. The result should be like this: 
    doc\book\tools\xsl

  Perl
  ----
  Use a (native Windows) Perl 5.8.0 or better with the libwin32 bundle for
  automating the setup.

  Installation notes:
    If you don't want to use Active Perl, then it's trivial to compile Perl by
    yourself if you have MS VC5 or better. Just remember to compile the Perl
    modules included in libwin32 when Perl itself is done.

Making a distro
===============

  The programs/scripts in the packages\win32-innosetup\tools folder will take
  care of making the Subversion documentation (a Windows HTML help file) from
  the sources in the doc directory of the subversion repository and setting
  version info on the setup. Just follow the steps below and you're set:

  1. Make sure that all the programs needed by INNO are installed as described
     earlier in this file.
  2. If you haven't done it already: Copy the file "paths_inno_src.iss" from
     the packages\win32-innosetup\templates folder to packages\win32-innosetup
     in your WC and edit it according to the documentation inside it.
  3. Make sure that all the files to include in the setup are where they are
     supposed to be according to the paths_inno_src.iss file.
  4. Now, you have two diffrent ways of making the documentation and the setup:

     A. Change directory (cd) to the packages\win32-innosetup\tools folder on
        your working Subversion repository and run the following command and
        follow the instructions:
            mk_distro
         
     B. You may want to make a automatic setup (nightly build, anything else),
        just run the packages\win32-innosetup\tools\mk_distro file as:
            path\to\packages\win32-innosetup\tools\mk_distro -a

  A shiny new svn-X.XX.X-rXXXX-setup.exe should now be in your path_setup_out
  folder if you have done everything right.

Good luck!

