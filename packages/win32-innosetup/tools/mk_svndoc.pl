#!perl
##########################################################################
# FILE       mk_svndoc
# PURPOSE    Making MS HTML-help from the Subversion source documentation
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

##########################################################################
# INCLUDED LIBRARY FILES
use strict;
use File::Find;
use Cwd;
use Win32;
require 'cmn.pl';

##########################################################################
# FUNCTION DECLARATIONS
sub Main;
sub CheckForProgs;
sub CntHhc;
sub CntHhcHead;
sub CntHhp;
sub CntHhpHead;
sub CopyFiles;
sub MkDirP;

##########################################################################
# CONSTANTS AND GLOBAL VARIABLES
# Sorces and destinations
my $g_PathDocRoot='..\..';
my $g_PathMiscIn=&cmn_ValuePathfile('path_setup_in');
my $g_PathDocDest="$g_PathMiscIn\\doc";
my %g_FilesToCpAndConv=
    (
        'COPYING', 'subversion\SubversionLicense.txt',
        'doc\user\lj_article.txt', 'doc\lj_article.txt',
        'doc\programmer\WritingChangeLogs.txt', 'doc\WritingChangeLogs.txt', 
    );

my %g_XmlFiles2Copy =
    (
        'doc\book\book\*.xml',                    'tools\doc\book',
        'doc\book\book\images\*.png',             'tools\doc\book\images',
        'doc\book\misc-docs\*.xml',               'tools\doc\misc-docs',
        'doc\book\tools\dtd\*.*',                 'tools\doc\tools\dtd',
        'doc\book\tools\xsl\*.*',                 'tools\doc\tools\xsl'
    );

# Programs needed for making the documentation
my $g_Prog_hhc='';
my %g_ProgsInPath=
    (
        'libxslt are needed for converting the XML-documentaion.',
            'xsltproc.exe',
        'iconv are needed by libxslt and libxslt for converting the XML-documentaion.',
            'iconv.exe',
        'libxml2 are needed libxslt by for converting the XML-documentaion.',
            'libxml2.dll'
    );

##########################################################################
# PROGRAM ENTRANCE
Main;

##########################################################################
# FUNCTION DEFINITIONS
#-------------------------------------------------------------------------
# FUNCTION   Main
# DOES       This is the program's main function
sub Main
{
    my $CntSvnDocHhp='';
    my $CntHhc='';
    my @Books=('book', 'misc-docs');
    my %XlsParams='';

    #In $g_PathMiscIn is relative
    #my $DirOrig = getcwd;
    chdir "..";

    &MkDirP ($g_PathDocDest);
    
    &CheckForProgs;
    &CopyFiles;

    chdir 'tools/doc';
    system ("copy ..\\mk_htmlhelp.bat mk_htmlhelp.bat");
    system ("copy ..\\..\\templates\\svn-doc.css svn-doc.css");
    system ("copy ..\\..\\images\\svn_bck.png svn_bck.png");
    
    system ("mk_htmlhelp.bat");
    
    $CntSvnDocHhp=&CntHhpHead;
    $CntSvnDocHhp= $CntSvnDocHhp . &CntHhp('book/book.hhp', 'book');
    $CntSvnDocHhp= $CntSvnDocHhp . &CntHhp('misc-docs/misc-docs.hhp', 'misc-docs');

    open (FH_HHP, ">" . "svn-doc.hhp");
        print FH_HHP $CntSvnDocHhp;
    close (FH_HHP);

    $CntHhc=&CntHhcHead;
    $CntHhc = $CntHhc . &CntHhc('book/book.hhc', 'book');
    $CntHhc = $CntHhc . &CntHhc('misc-docs/misc-docs.hhc', 'misc-docs');
    $CntHhc = "$CntHhc    </BODY>\n</HTML>\n";

    open (FH_HHC, ">" . "toc.hhc");
        print FH_HHC $CntHhc;
    close (FH_HHC);
    
    system ("$g_Prog_hhc svn-doc.hhp");

    chdir '../..';
    system ("copy /Y tools\\doc\\svn-doc.chm $g_PathDocDest");
    chdir 'tools';
    system ("rmdir /Q /S doc");
}

#-------------------------------------------------------------------------
# FUNCTION   CheckForProgs
# DOES       Checking if required programs exists
sub CheckForProgs
{
    my $Key = '';
    my $Value = '';
    my @MissingProgs;
    my @SysPath;
    my $bMissingProgs=0;
    my $Prog2CheckDesc='';
    my $Prog2Check='';
    
    # Fill the %PATH% in @SysPath
    @SysPath = split (/;/, $ENV{PATH});

    # Check for the needed programs who should be in the %PATH%
    while (($Prog2CheckDesc, $Prog2Check) = each %g_ProgsInPath)
      {
        my $bProg2CheckExists=0;

        for (@SysPath)
          {
            if (-e "$_\\$Prog2Check")
              {
                $bProg2CheckExists=1;
                last;
              }
          }

        if (! $bProg2CheckExists)
          {
            $bMissingProgs = 1;
            push @MissingProgs, $Prog2CheckDesc;
          }
      }
    
    # Check for MS HTML help compiler
    $Key = 'HKLM/Software/Microsoft/Windows/CurrentVersion/App Paths/hhw.exe';
    $Value = 'Path';
    $g_Prog_hhc = &cmn_RegGetValue ($Key, $Value);
    $g_Prog_hhc = "$g_Prog_hhc\\hhc.exe";
    
    if (! -e $g_Prog_hhc)
      {
        $bMissingProgs = 1;
        push @MissingProgs, 'Microsoft HTML Help Workshop are needed for making the HTML-help file';
      }


    if ($bMissingProgs)
      {
        my $Msg="One or more required programs needed for making the docs are missing:\n\n";
   
        for (@MissingProgs)
          {
            $Msg=$Msg . "  - $_\n";
          }
          
          $Msg=$Msg . "\nPlease, check that everything are installed properly as described in\n";
          $Msg=$Msg . "the documentation in packages\\win32-innosetup\\tools\\readme.txt\n";
          Win32::MsgBox($Msg, 0+MB_ICONSTOP, 'ERROR: Missing required programs.');
          exit 1;
      }
}

#-------------------------------------------------------------------------
# FUNCTION   CntHhc
# DOES       Getting and returning the menu entries from a HTML-help
#            toc file 
sub CntHhc
{
    my ($HhcFile, $book) = @_;
    my $CntHhcFile='';
    my $iCount=0;

    open (FH_HHCFILE, $HhcFile);
    while (<FH_HHCFILE>)
		  {
        chomp($_);
        $_ =~ s/^\s+//;
	      $_ =~ s/\s+$//;

        if ($_ eq '<UL>')
          {
            $iCount = 1;
          }
        elsif ($_ eq '</BODY>')
          {
            $iCount = 0;
          }

        if ($iCount == 1)
          {
            $_ = "    $_" if ($_ =~ /<\/OBJECT>/);
            $_ = "      $_" if ($_ =~ /<param /);

            if ($CntHhcFile)
              {
                $CntHhcFile = "$CntHhcFile" . "$_\n";
              }
            else
              {
                $CntHhcFile = "$_\n";
              }
          }          
      }
        
    close (FH_HHCFILE);

    return $CntHhcFile;
}

#-------------------------------------------------------------------------
# FUNCTION   CntHhp
# DOES       Getting and returning the files in a HTML-help project file
#            and prefix the files with the book name folder name
sub CntHhp
{
    my ($HhpFile, $book) = @_;
    my $CntHhpFile='';
    my $iCount=0;

    open (FH_HHPFILE, $HhpFile);
    while (<FH_HHPFILE>)
		  {
			  chomp($_);
        if (/\[FILES]/)
          {
            $iCount = 1;
          }

        if ($iCount > 0)
          {
            $iCount++;
          }

        if ($iCount > 2)
          {
            if ($CntHhpFile)
              {
                $CntHhpFile = "$CntHhpFile" . "$book/$_\n";
              }
            else
              {
                $CntHhpFile = "$book/$_\n";
              }
          }       
      }
    close (FH_HHPFILE);

    return $CntHhpFile;
}
#-------------------------------------------------------------------------
# FUNCTION   CntHhcHead
# DOES       Returning the header of a HTML-help toc file
sub CntHhcHead
{
    my $CntHead="<HTML>";
    $CntHead="$CntHead\n<HEAD>";
    $CntHead="$CntHead\n</HEAD>";
    $CntHead="$CntHead\n  <BODY>";
    $CntHead="$CntHead\n<OBJECT type=\"text/site properties\">";
    $CntHead="$CntHead\n	<param name=\"ImageType\" value=\"Folder\">";
    $CntHead="$CntHead\n</OBJECT>\n";

    return $CntHead;
}

#-------------------------------------------------------------------------
# FUNCTION   CntHhpHead
# DOES       Returning the header of a HTML-help project file
sub CntHhpHead
{
    my $CntHead="[OPTIONS]";
    #$CntHead="$CntHead\nBinary TOC=Yes";
    $CntHead="$CntHead\nCompatibility=1.1 or later";
    $CntHead="$CntHead\nCompiled file=svn-doc.chm";
    $CntHead="$CntHead\nContents file=toc.hhc";
    $CntHead="$CntHead\nDefault Window=Main";
    $CntHead="$CntHead\nDefault topic=book/index.html";
    $CntHead="$CntHead\nDisplay compile progress=No";
    $CntHead="$CntHead\nLanguage=0x409 Engelska (USA)";
    $CntHead="$CntHead\nTitle=Subversion Documentation";

    $CntHead="$CntHead\n\n[WINDOWS]";
    $CntHead="$CntHead\nMain=\"Subversion Documentation\",\"toc.hhc\",,\"book/index.html\",\"book/index.html\",,,,,0x23520,,0x60300e,,,,,,,,0\n";
    $CntHead="$CntHead\n\n[INFOTYPES]";
    $CntHead="$CntHead\n\n[FILES]";
    $CntHead="$CntHead\nsvn-doc.css";
    $CntHead="$CntHead\nsvn_bck.png\n";
    
    return $CntHead;
}

#-------------------------------------------------------------------------
# FUNCTION   CopyAndEolU2W
# DOES       Converts Unix eol's to Windows eol's in a file and saves it to
#            another location.
sub CopyAndEolU2W
{
    my $FileSrc='';
    my $FileDest='';
    my $FileCnt='';

    while (($FileSrc, $FileDest) = each %g_FilesToCpAndConv)
      {
        $FileSrc = "$g_PathDocRoot\\$FileSrc";
        $FileDest = "$g_PathMiscIn\\$FileDest";
        print "Copying and converting EOL's from $FileSrc to $FileDest\n";

        open (FH_SRC, $FileSrc);
            while (<FH_SRC>)
              {
                chomp($_);
                $_ = "$_\r\n";
                
                if ($FileCnt)
                  {
                    $FileCnt = $FileCnt . $_;
                  }
                else
                  {
                    $FileCnt = $_;
                  }
              }
         close (FH_SRC);
               
        open (FH_DEST, ">" . $FileDest);
            print FH_DEST $FileCnt;
        close (FH_DEST);

        $FileCnt='';
      }
}

#-------------------------------------------------------------------------
# FUNCTION   CopyFiles
# DOES       Copying all the files needed to make the MS HTML-help file
#            to the doc folder
sub CopyFiles
{
    
    my $PathSrc='';
    my $PathDest='';
    my @PathsDest;
    my @SubPaths;

    # Copy the files who should 
    &CopyAndEolU2W;

    # Make sure that the destination folders exists and copy files
    while (($PathSrc, $PathDest) = each %g_XmlFiles2Copy)
      {
        &MkDirP("$PathDest");
        print "Copying from: $PathSrc To: $PathDest\n";
        system ("xcopy /Y /S $g_PathDocRoot\\$PathSrc $PathDest > NUL");
      }    
}

#-------------------------------------------------------------------------
# FUNCTION   MkDirP
# DOES       Making a directory. Similar to unix's mkdir -p
sub MkDirP
{
    my $Dir=$_[0];
    my @SubPaths;

    
    
    if (! -e $Dir)
      {
        @SubPaths = split (/\\/, $Dir);
        my $Dir2Make='';
        for (@SubPaths)
          {
            if ($Dir2Make)
              {
                $Dir2Make = "$Dir2Make\\$_";
              }
            else
              {
                $Dir2Make = $_;
              }

            if (! -e $Dir2Make)
              {
                system ("mkdir $Dir2Make");
              }
          }
      }
}
