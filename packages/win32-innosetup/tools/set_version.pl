#!perl
################################################################################
# FILE     set_version.pl
# PURPOSE  Setting version info on misc. files for Inno Setup
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

################################################################################
# INCLUDED LIBRARY FILES
use strict;
use Cwd;
use Win32;
require 'cmn.pl';

################################################################################
# FUNCTION DECLARATIONS
sub Main;
sub Mk7zConf;
sub Mk7zSfxBat;
sub Path7Zip;
sub PathSetupOut;
sub PathSvn;
sub SetVerSvnIss;
sub SetVersion;
sub SvnVersion;

################################################################################
# CONSTANTS AND GLOBAL VARIABLES
my $g_AutoRun='';

##########################################################################
# PROGRAM ENTRANCE
Main;

################################################################################
# FUNCTION DEFINITIONS
#-------------------------------------------------------------------------------
# FUNCTION   Main
# DOES       This is the program's main function
sub Main
{
    my $Arg=$ARGV[0];

    if ($Arg eq "-a")
      {
        $g_AutoRun="y";
      }
    
    my ($SvnVersion, $SvnRelease) = &SetVersion;
    my $PathSetupOut = &PathSetupOut;
    my ($Path7Zip_exe, $Path7Zip_sfx) = &Path7Zip;



    if (! $g_AutoRun)
      {
        print "Setting version $SvnVersion and release $SvnRelease on...\n";
      }
    
    #Make mk7zsfx.bat and 7z.conf in $PathSetupOut
    &Mk7zSfxBat($Path7Zip_exe, $Path7Zip_sfx, $SvnVersion,
                 $SvnRelease, $PathSetupOut);
    &Mk7zConf($SvnVersion, $SvnRelease, $PathSetupOut);
    
    #Set version info on svn.iss
    &SetVerSvnIss($SvnVersion, $SvnRelease);
}

#-------------------------------------------------------------------------------
# FUNCTION   Mk7zConf
# DOES       Making the 7z.conf file
sub Mk7zConf
{
    my ($SvnVersion, $SvnRelease, $PathSetupOut) = @_;
    my $Mk7zConf_cnt='';
    my $DirOrig = getcwd;
    my %Values =
		  (
        tv_version  => $SvnVersion,
        tv_release  => $SvnRelease
      );

    $Mk7zConf_cnt = &cmn_Template("../templates/7z.conf", \%Values);

    if (! $g_AutoRun)
      {
        print "  7z.conf in the Inno output directory.\n";
      }

    chdir ".."; #In case the path is relative

    open (FH_MKSEVZCONF, ">" . "$PathSetupOut/7z.conf") ||
      die "ERROR: Could not open $PathSetupOut\\7z.conf\n";
		    print FH_MKSEVZCONF $Mk7zConf_cnt;
	  close (FH_MKSEVZCONF);

    chdir $DirOrig;
}

#-------------------------------------------------------------------------------
# FUNCTION   Main
# DOES       Making the mk7zsfx.bat file
sub Mk7zSfxBat
{
    my ($Path7Zip_exe, $Path7Zip_sfx, $SvnVersion,
        $SvnRelease, $PathSetupOut) = @_;
    my $Mk7zSfxBat_cnt='';
    my $DirOrig = getcwd;
    my %Values =
		  (
				tv_7zip_exe => $Path7Zip_exe,
				tv_7zip_sfx => $Path7Zip_sfx,
        tv_version  => $SvnVersion,
        tv_release  => $SvnRelease
      );


    $Mk7zSfxBat_cnt = &cmn_Template("../templates/mk7zsfx.bat", \%Values);

    chdir ".."; #In case the paths is relative

    if (! $g_AutoRun)
      {
        print "  mk7zsfx.bat in the Inno output directory.\n";
      }

    open (FH_MKSFXBAT, ">" . "$PathSetupOut/mk7zsfx.bat") ||
      die "ERROR: Could not open $PathSetupOut\\mk7zsfx.bat\n";
		    print FH_MKSFXBAT $Mk7zSfxBat_cnt;
	  close (FH_MKSFXBAT);

    chdir $DirOrig;
}
#-------------------------------------------------------------------------
# FUNCTION Path7Zip
# DOES     Finding and returning the paths of 7z.exe and 7zS.sfx
sub Path7Zip
{
    my $Path7Zip_exe='';
    my $Path7Zip_sfx='';
    my $Path7Zip= &cmn_RegGetValue('HKLM/SOFTWARE/7-ZIP', 'Path');

    print "Checking for 7-zip.." if (! $g_AutoRun);

    if (-e "$Path7Zip/7z.exe")
      {
          $Path7Zip_exe = "\"$Path7Zip\\7z.exe\"";
      }

    if (-e "$Path7Zip/7zS.sfx")
      {
          $Path7Zip_sfx = "\"$Path7Zip\\7zS.sfx\"";
      }

    if ($Path7Zip_exe && $Path7Zip_exe)
      {
         print "ok. Found in $Path7Zip\n" if (! $g_AutoRun);
      }
    else
      {
        die "ERROR: Could not find 7-zip. Make sure it's installed correctly\n";
      }

    return ($Path7Zip_exe, $Path7Zip_sfx);
}

#-------------------------------------------------------------------------------
# FUNCTION PathSetupOut
# DOES     Finding and returning the current svn.exe path as of
#          ..\svn_iss_dyn.iss
sub PathSetupOut
{
    my $SetupOut = &cmn_ValuePathfile('path_setup_out');
  
    if ( ! -e "../$SetupOut")
      {
        die "ERROR: Could not find $SetupOut in ..\\paths_inno_src.iss\n";
      }
    
    return $SetupOut;
}

#-------------------------------------------------------------------------------
# FUNCTION PathSvn
# DOES     Finding and returning the current svn.exe path as of
#          ..\paths_inno_src.iss
sub PathSvn
{
    my $RetVal = &cmn_ValuePathfile('path_svnclient');
    my $ErrMsg="ERROR: File not found: Could not find svn.exe in:\n  $RetVal\n";
    $ErrMsg=$ErrMsg . "Please, check that the path_svnclient variable in the ";
    $ErrMsg=$ErrMsg . "..\\paths_inno_src.iss\n";
    $ErrMsg=$ErrMsg . "file are correct and try again\n";
    
    if (-e "$RetVal\\svn.exe")
      {
        $RetVal="$RetVal\\svn.exe";
      }
    else
      {
        die $ErrMsg;
      }

    return $RetVal;
}

#-------------------------------------------------------------------------------
# FUNCTION SetVersion
# DOES     Gets and returns version info from userinput
sub SetVersion
{
    my ($SvnVersion, $SvnRelease) = &SvnVersion;
    my $Input='';

    if (! $g_AutoRun)
      {
        print "\nsvn.exe that's mentioned in your paths_inno_src.iss file have ",
          "told me that the\n",
          "version you want to make a distro from is $SvnVersion and that the ",
          "revision is\n",
          "$SvnRelease. You can confirm this by hitting the ENTER button ",
          "(wich then sets the numbers\n",
          "inside the brackets) or write some new data followed by the ENTER",
          " button.\n\n",
          "Please, make sure that svn.iss is not opened by another ",
          "applications before you continue:\n\n";
          
          print "  Version [$SvnVersion]: ";
          
        chomp ($Input = <STDIN>);

        if ($Input)
          {
            $SvnVersion = $Input;
          }

        print "  Release [$SvnRelease]: ";
        chomp ($Input = <STDIN>);
        if ($Input)
          {
            $SvnVersion = $Input;
          }
      }

    return ($SvnVersion, $SvnRelease);
}

#-------------------------------------------------------------------------------
# FUNCTION SetVerSvnIss
# DOES     Setting version info on svn.iss
sub SetVerSvnIss
{
    my ($SvnVersion, $SvnRelease) = @_;
    my $IssFileCnt='';

    if (! -e '../svn_version.iss')
      {
        system ("copy ..\\templates\\svn_version.iss ..");
      }

    print "  svn_version.iss in the Inno Setup directory.\n" if (! $g_AutoRun);
    
    open (FH_ISSFILE, '../svn_version.iss') || die "ERROR: Could not open ..\\svn_version.iss";
    while (<FH_ISSFILE>)
      {
			  chomp($_);

        if ($IssFileCnt)
          {
            $IssFileCnt="$IssFileCnt\n";
          }

        if (/^#define svn_version/)
          {
              $IssFileCnt= $IssFileCnt . "#define svn_version \"$SvnVersion\"";
          }
        elsif (/^#define svn_release/)
          {
              $IssFileCnt= $IssFileCnt . "#define svn_release \"$SvnRelease\"";
          }
        else
          {
              $IssFileCnt= $IssFileCnt . $_;
          }
      }
    close (FH_ISSFILE);  

    $IssFileCnt="$IssFileCnt\n";

    open (FH_ISSFILE, ">" . '../svn_version.iss')
      || die "ERROR: Could not open ..\\svn_version.iss";
		    print FH_ISSFILE $IssFileCnt;
	  close (FH_ISSFILE);
}

#-------------------------------------------------------------------------------
# FUNCTION SvnVersion
# DOES     Getting and returns the version and release number from the svn.exe
#          as of the binary to include in the distro
sub SvnVersion
{
    my $Svn = &PathSvn;
    my $SvnRetVal='';
    my ($SvnVersion, $SvnRelease) ='';
 
    $Svn = "\"$Svn\"";
    $SvnRetVal = `$Svn --version`;
    $SvnRetVal =~ s/svn, version//;
    $SvnRetVal =~ s/(^.*)\).*/$1/;

    ($SvnVersion, $SvnRelease) = split (/\(/, $1);

    $SvnVersion =~ s/^\s+//;
	  $SvnVersion =~ s/\s+$//;
    $SvnRelease =~ s/r//;
    $SvnRelease =~ s/dev build/_dev-build/;

    return ($SvnVersion, $SvnRelease);
}
