Information about the installation program for Subversion for Windows
=====================================================================

$LastChangedDate$

CONTENTS:
=========
  * Introduction
  * Directory structure
  * Programs used for the Subversion installer and instructions
    - Inno Setup
    - ISTool
    - 7-zip
    - GNU diffutils
    - Perl
    - Packages for converting XML documentation
    - MS HTML Help Workshop
  * How the documentation is done    
  * How the setup is done
  * File Structure for the installer project

Introduction
============

  This document describes the packages\win32-innosetup directory of the
  Subversion repository and tells you how you can roll out your own
  Windows installer for Subversion.
  
  If you haven't none it already:  -Please, Check out the subversion
  sources to a place you like and download the programs and packages
  from the links below. Reading the "Directory structure" part should
  be the next.

  Inno Setup
  ----------
  Inno Setup (IS) 3.0.4 or better with "My Inno Setup Extentions"
  (ISX) and "Inno Setup Pre Processor" (ISPP) included:
    http://www.wintax.nl/isx/

  ISTool
  ------
  ISTool 3.0.4.1 or better:
    http://www.bhenden.org/istool/

  7-zip
  -----
  7-Zip 2.30 Beta 24 or better:
    http://www.7-zip.org/

  GNU diffutils
  -------------
  Get Cygwin with diffutils-2.8.1 or better from:  
    http://www.cygwin.com/

  Perl
  ----
  Perl 5.8.0 or better with the libwin32 bundle (included in
  ActivePerl):
    http://www.perl.com/
    http://www.cpan.org/modules/by-module/Win32/ (libwin32)

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
  Visible folders looks like this: [ ] and hidden folders like this: [h].
  The childs of the svn:ignore'd folders are not marked as [h].
  Do you think that is looks complicated? -Dont worry! The programs in the
  tools folder takes care of copying and preparing files.
  
  [ ] win32-innosetup           (svn.iss, misc. and main folder for Inno Setup)
   +->[ ] images                             (Various images used by the setup)
   |->[h] in                               (ISX expects most of the files here)
   |   +->[ ] apache2                                    (apache related files)
   |   |   +->[ ]modules                                       (mod_dav_svn.so)
   |   +->[ ] berkeley                                  (the berkeley binaries)
   |   +->[ ] doc                             (svn-doc.chm and other documents)
   |   +->[ ] helpers                 (Programs needed by setup and subversion)
   |   |   +->[ ] cygdiff                 (Cygwin version of the GNU diffutils)
   |   |   +->[ ] include                               (various include files)
   |   |   |   +->[ ] berkeley                         (berkeley include files)
   |   |   +->[ ] lib                                   (various library files)
   |   |   |   +->[ ] berkeley                         (berkeley library files)
   |   |   +->[ ] ssh                                            (ssh binaries)
   |   |   +->[ ] subversion              (subversion binaries and other files)
   |->[h] out                            (this is where the finished setup end)
   +->[ ] tools           (misc. programs/scripts for making and helping setup)
   |   +->[ ] svnpath                     (sources and the svnpath.exe program)
   |   +->[ ] templates                       (templates used by various tools)

   
Programs used for the Subversion Windows installer
==================================================

  Inno Setup
  ----------
  The installation program is the exellent Inno Setup made by Jordan
  Russell. IS and friends are probably all you need for 99% of any
  Windows installer needs made for the various flavors of Windows and
  has proven to be extremely reliable and stable.

  The Inno Setup used by Subversion are extended with "My Inno Setup
  Extentions" made by Martijn Laan and ISPP made by Alex Yackimoff.
  ISX includes IS and ISPP so all you need to do, is :

  Jordan Russell's (the Inno Setup creater) homepage:
    http://www.jrsoftware.org/
    
  Installation notes: None

  ISTool
  ------
  A good installation script for any installation programs are
  usually very complicated and requires good script editing software.
  The program used for this is ISTool and it's syntax high-lighning
  makes it the perfect companion to IS and friends.
  
  The author - Bjørnar Henden are doing a great job by updating his
  program each time Inno Setup are updated.
  It's also includes full support for "My Inno Setup Extentions" (see
  above).

  Installation notes: None

  7-zip
  -----
  7-zip offers a very high compression ratio! Usually, one use the
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
  This c program are used for updating the user's path to include/
  exclude the Subversion path after installing/un-installing
  Subversion.
  You can find the sources for this program in the Subversion source
  tree under packages\win32-innosetup\tools\svnpath
  
  Have a look in the file main.c for info on how to compile the
  program.
 
  libxml, libxslt and iconv
  -------------------------
  Installation notes:
    Unpack the zip-files and place the contents of the 'lib' and
    'util' folders from each unzipped packages in a folder which
    is mentioned in your PATH environment variable.
    
  docbook-xsl
  -----------
  Unzip the files inside docbook-xsl-*.**.*.zip to a folder named
  xsl which resides under doc\book\tools in your working copy of the
  subversion repository. The result should be like this:
    doc\book\tools\xsl

  Perl
  ----
  We are using some UTF-8 encoding for the setup so you need Perl
  5.8.0 or better with the libwin32 bundle for automating the setup.

  Installation notes:
    If you don't want to use Active Perl, then it's trivial to
    compile Perl by yourself if you have MS VC5 or better. Just
    remember to compile the Perl modules included in libwin32 when
    Perl itself is done.

  svnpath
  -------
  svnpath helper program for Inno Setup and edits the end user's system
  path. You can find it in the tools folder.

How the documentation is done
=============================

  Under preparation
  
Some tips/info about using Inno Setup and 7-zip
===============================================

  Inno Setup are using gzip as standard but also bundles bzip2. The
  problem is that every single file seems to be compressed in stead
  of a bigger setup data file which make the use of bzip2 inefficient.
  It's here 7-zip comes to salvation. I don't let Inno compress the
  files at all when it's compiling the setup program and the setup
  program are compiled without the "use setup loader" directive. This
  make the compressed file made by 7-zip smaller.
  
  Now it's time to right click on the files made by the Setup
  compiler and choose "7-zip -> Add to Archive...". A dialog will
  show up and now, you can name it as setup.7z.
  When setup.7z are made, it's time to make an SFX of it. This is
  done by running the batch file mk7zsfx.bat (make sure that paths
  and version numbers are correct in mk7zsfx.bat and 7z.conf).

File Structure for the installer project
========================================
  Under preparation. You will get the picture by looking at the file
  svn.iss.

Good luck!
