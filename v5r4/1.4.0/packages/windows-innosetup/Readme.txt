Information about the installation program for Subversion for Windows
=====================================================================
$LastChangedDate$


CONTENTS:
=========
  * Introduction (read first!)
  * Dynamic build data
  * Programs used for the Subversion installer and instructions
    - Inno Setup
    -
    - Perl
    - Packages for converting XML documentation
    - MS HTML Help Workshop
  * Making a distro
  * svn-x.xx.x-setup.exe's command line options


Introduction
============

  This document describes the packages\windows-innosetup directory of the
  Subversion repository and tells you how you can roll out your own Windows
  installer for Subversion.

  If you have trouble, make sure that you use the packages versions that are
  noted here (if noted) before asking for help.

  If you haven't done it already:  -Please, Check out the subversion sources to
  a place you like and download the programs and packages from the links below.
  Reading the "Directory structure" part should be the next.

  Inno Setup
  ----------
  Inno Setup QuickStart Pack 5.0.8. This package gives you Inno Setup (IS)
  and a sutable version of "Inno Setup Pre Processor" (ISPP):
    http://www.jrsoftware.org/isdl.php

  UninsHs
  -------
  UninsHs-1.6 adds "Modify/Repair/Uninstall" capabilities for the installer.
    http://www.han-soft.biz/uninshs.php

  Svnbook
  -------
  We need the sources of this book in order to make the MS HTML-documentation.
  Fire up a console window (a so called DOS box) and download the sources via
  Subversion to a location by your choice:
    svn co http://svn.red-bean.com/svnbook/trunk svnbook


  Perl
  ----
  Perl 5.8.0 or better with the libwin32 bundle (included in ActivePerl):
  	http://www.activestate.com/ActivePerl/
  or make your own (and compile the module libwin32):
    http://www.perl.com/
    http://www.cpan.org/modules/by-module/Win32/ (libwin32-X.XXX.zip)

  libxml
  ------
  Point your browser to:
    http://www.zlatkovic.com/pub/libxml/
  and grab the following packages:
    - libxml2-2.6.17
    - libxslt-1.1.12+
    - iconv-1.9.1
    - zlib-1.2.1

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


Dynamic build data
==================

  The setup system must consider many dynamic data when creating the
  installation program. This is things such as paths, different makers of the
  setup (they may have different compilers and paths in their system) and that
  binarys and contents might vary.

  All this data are maintained by the file svn_dynamics.iss which has a lot
  variables that is processed by Inno Setup Pre Processor (ISPP) during the
  compiling of the setup.
  A template can be found in the packages\windows-innosetup\templates
  directory. Copy this file to the packages\windows-innosetup directory and
  edit it according to the documentation inside it. This file is not under
  version control (the template is) since the contents will vary depending on
  the system they are in.

  The Inno setup file lives under the packages\windows-innosetup directory
  of the Subversion repository and are using folders which are both visible
  and "hidden". The hidden folders have the svn property svn:ignore and only
  exists on your machine.

  The setup system gets its files (and have files) from two kinds of places:
  * Static:  This files are always somewhere in the repository.
  * Dynamic: This files can be picked up anywhere from your computer (even from
             the repository).

  Visible folders looks like this: [ ] and hidden folders like this: [h].
  
  Do you think that is looks complicated? Don't worry! The programs in the
  tools folder takes care of copying and preparing files when your 
  svn_dynamics.iss file are edited and set correctly.

  Static paths (in the Subversion repository):
  -------------  
  [ ] windows-innosetup                   (svn.iss, main folder for Inno Setup)
   +->[ ] images                             (Various images used by the setup)
   +->[h] in    (you can set your path_setup_in here if you want to, see below)
   +->[h] out  (you can set your path_setup_out here if you want to, see below)
   +->[ ] templates        (misc templates used by various tools and the setup)
   +->[ ] tools            (misc. stuff for making and helping to make a setup)
   |   +->[ ] svnpath                   (C sources for the svnpath.exe program)

  Dynamic paths (files from anywhere on your machine)
  ---------------------------------------------------

  This paths are determined by values in the file svn_dynamics.iss. The value
  names of this path variables is:
  
  Path variables:     Setup files:
  ---------------     ---------------------------------------------------------
  path_setup_out      Where svn-X.XX.X-rXXXX-setup.exe is to find after
                      compiling the setup
  path_setup_in       Contains misc. files to include in the setup
  path_is             Path to the Inno Setup executable's directory
  path_svnclient      svn.exe
  path_svnadmin       svnadmin.exe
  path_svnlook        svnlook.exe
  path_svnserve       svnserve.exe
  path_svnversion     svnversion.exe
  path_svndumpfilter  svndumpfilter.exe
  path_davsvn         mod_dav_svn.so
  path_authzsvn       mod_authz_svn.so
  path_svnpath        svnpath.exe
  path_iconv          *.so
  path_brkdb_bin      db_*.exe, ex_*.exe, excxx_*.exe, libdb4*.dll, libdb4*.exp
  path_brkdb_lib      libdb4*.lib
  path_brkdb_inc      db.h, db_cxx.h)
  path_brkdb_inc2     cxx_common.h, cxx_except.h
  path_ssl            libeay32.dll, ssleay32.dll


Programs used for the Subversion Windows installer
==================================================

  Inno Setup
  ----------
  The installation program is the excellent Inno Setup made by Jordan Russell
  (with a lot of additional code by Martijn Laan, mostly the scripting part).
  IS and friends are probably all you need for 99% of any Windows installer
  needs made for the various flavours of Windows and has proven to be extremely
  reliable and stable.

  The Inno Setup used by Subversion are extended with "Inno Setup Pre
  Processor" made by Alex Yackimoff. "Inno Setup QuickStart Pack" includes
  both IS and ISPP so all you need is "Inno Setup QuickStart Pack"

  Installation notes: None

  ISTool
  ------
  A good installation script for any installation programs are usually very
  complicated and requires good script editing software.
  The program used for this is ISTool and it's syntax high-lightning makes it
  the perfect companion to IS and friends.

  The author - Bjørnar Henden are doing a great job by updating his program
  each time Inno Setup are updated.

  Installation notes: Can be retrieved by "Inno Setup QuickStart Pack"

  svnpath
  -------
  Inno Setup does not currently edit the systems PATH environment so we need
  svnpath.
  This C program is used for updating the user's path to include/exclude the
  Subversion path after installing/un-installing Subversion.
  You can find the sources for this program in the Subversion source tree under
  packages\windows-innosetup\tools\svnpath.
  Have a look in the file main.c for info on how to compile the program.

  If you don't want to compile it then download it from:
    http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=2728
  
  Unzip the file and put the svnpath.exe in the directory
    packages\windows-innosetup\tools\svnpath

  UninsHs
  -------
  Unpack the zipfile and place UninsHs.exe in packages\windows-innosetup

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
  resides under doc\book\tools in your working copy of the svnbook repository.
  Rename the unpacked top level folder from "docbook-xsl-x.xx.x" to "xsl", the
  result should be like this:
    src\tools\xsl

  Perl
  ----
  Use a (native Windows) Perl 5.8.0 or better with the libwin32 bundle for
  automating the setup.

  Installation notes:
    If you don't want to use Active Perl, then it's trivial to compile Perl by
    yourself if you have MS VC5 (or better) or MinGW. Just remember to compile
    the Perl modules included in libwin32 when Perl itself is done.


Making a distro
===============

  The programs/scripts in the packages\windows-innosetup\tools folder will take
  care of making the Subversion documentation (a Windows HTML help file) from
  the sources in the doc directory of the subversion repository and setting
  version info on the setup. Just follow the steps below and you're set:

  1. Make sure that all the programs needed by INNO are installed as described
     earlier in this file.

  2. If you haven't done it already: Copy the file "svn_dynamics.iss" from
     the packages\windows-innosetup\templates folder to
     packages\windows-innosetup in your WC and edit it according to the
     documentation inside it.

  3. Copy the file svn_version.iss from packages\windows-innosetup\templates to
     packages\windows-innosetup and edit it according to the documentation
	   inside it.

  4. Make sure that all the files to include in the setup are where they are
     supposed to be according to the svn_dynamics.iss file.

  5. Now, you have two different ways of making the documentation and the
     setup:
     A. Change directory (cd) to the packages\windows-innosetup\tools folder on
        your working Subversion repository and run the following command and
        follow the instructions:
            mk_distro

     B. You may want to make an automatic setup (nightly build, anything else),
        just run the packages\windows-innosetup\tools\mk_distro file:
            path\to\packages\windows-innosetup\tools\mk_distro -a

  A shiny new svn-x.xx.x-setup.exe should now be in your path_setup_out
  folder if you have done everything right.

  Good luck!

svn-x.xx.x-setup.exe's command line options
===========================================

  The text below are more or less copied directly from the Inno Setup Help file
  and describes the parameters that the Subversion installer Setup file
  accepts:

  The Setup program accepts optional command line parameters. These can be
  useful to system administrators, and to other programs calling the Setup
  program.

  /SP-
    Disables the This will install... Do you wish to continue? prompt at the
    beginning of Setup. Of course, this will have no effect if the
    DisableStartupPrompt [Setup] section directive was set to yes.

  /SILENT, /VERYSILENT
    Instructs Setup to be silent or very silent. When Setup is silent the
    wizard and the background window are not displayed but the installation
    progress window is. When a setup is very silent this installation progress
    window is not displayed. Everything else is normal so for example error
    messages during installation are displayed and the startup prompt is (if
    you haven't disabled it with DisableStartupPrompt or the '/SP-' command
    line option explained above) 

    If a restart is necessary and the '/NORESTART' command isn't used (see
    below) and Setup is silent, it will display a Reboot now? message box. If
    it's very silent it will reboot without asking. 

  /NOCANCEL 
    Prevents the user from cancelling during the installation process, by
    disabling the Cancel button and ignoring clicks on the close button. Useful
    along with /SILENT. 

  /NORESTART 
    Instructs Setup not to reboot even if it's necessary. 

  /LOADINF="filename" 
    Instructs Setup to load the settings from the specified file after having
    checked the command line. This file can be prepared using the '/SAVEINF='
    command as explained below. 

    Don't forget to use quotes if the filename contains spaces. 

  /SAVEINF="filename" 
    Instructs Setup to save installation settings to the specified file. 

	  Don't forget to use quotes if the filename contains spaces. 

  /DIR="x:\dirname" 
    Overrides the default directory name displayed on the Select Destination
    Location wizard page. A fully qualified pathname must be specified. 

   /GROUP="folder name" 
    Overrides the default folder name displayed on the Select Start Menu Folder
    wizard page. If the [Setup] section directive DisableProgramGroupPage was
    set to yes, this command line parameter is ignored. 

  /NOICONS 
    Instructs Setup to initially check the Don't create any icons check box on
    the Select Start Menu Folder wizard page. 

  /COMPONENTS="comma separated list of component names" 
    Overrides the default components settings. Using this command line
    parameter causes Setup to automatically select a custom type.
