# ==============================================================================
# Common Perl routines for the inno setup Perl helper scripts.
# ==============================================================================
# Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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
# ==============================================================================

#-------------------------------------------------------------------------------
# FUNCTION   cmn_IniDir
# DOES       Returns the directory where the initialization file is. The
#            dir is application directory of the current user 
sub cmn_IniDir
{
    my $DirAppData='';
  
    # The registry is the safe way of retrieving the Application data directory,
    # but we let the environment variable %APPDATA% have the priority. This 
    # should work on every Win32 platform.
    if ($ENV{'APPDATA'})
      {
        $DirAppData = $ENV{'APPDATA'};
      }
    else
      {
        my $Key = 'HKCU/Software/Microsoft/Windows/CurrentVersion/Explorer/Shell Folders';
        my $Value = 'AppData';
        $DirAppData = &cmn_RegGetValue ($Key, $Value);
      }

    return "$DirAppData\\Subversion";
}

#-------------------------------------------------------------------------------
# FUNCTION   cmn_RegGetValue
# RECEIVES   The Key and value.
# DOES       Returns a value data  from the registry. If the value is
#            omitted then the default value data for the key is returned
sub cmn_RegGetValue
{
    use Win32::TieRegistry;
  
    my ($Key, $Value) = @_;
  
    # Replace back slashes with slashes
    $Key =~ s/\\/\//g;
  
    # Do some filtering if the caller includes HKLM in stead of HKEY_LOCAL_MACHINE
    # or the Win32::TieRegistry shortcut LMachine and so on
    $Key =~ s/^HKCC/CConfig/;
    $Key =~ s/^HKCR/Classes/;
    $Key =~ s/^HKCU/CUser/;
    $Key =~ s/^HKDD/DynData/;
    $Key =~ s/^HKLM/LMachine/;
    $Key =~ s/^HKPD/PerfData/;
    $Key =~ s/^HKUS/Users/;
  
    $Registry->Delimiter("/");

    return $Registry -> {"$Key//$Value"};
}

#-------------------------------------------------------------------------------
# FUNCTION   incCmn_Template
# RECEIVES   An hash table with values, template name
# RETURNS    The contents of the template with the received values
# DOES       Reading from a template and fills in values in <%..%> tags if any
sub cmn_Template
{
 	  my ($sFile, $sValues) = @_;
 	  my $sFileCnt='';

 	  local $/;
 	  local *FH_TPL;

 	  open (FH_TPL, "< $sFile\0")	|| return;
 	      $sFileCnt = <FH_TPL>;
 	  close (FH_TPL);

 	  $sFileCnt =~ s{<% (.*?) %>}

 	  {exists ( $sValues->{$1}) ? $sValues->{$1} : '' }gsex;

 	  return $sFileCnt;
}

#-------------------------------------------------------------------------------
# FUNCTION cmn_ValuePathfile
# DOES     Get and returns a ISPP variable value from a file
sub cmn_ValuePathfile
{
    my $VarISPP = $_[0];
    my $RetVal='';
    my $ErrNoPathFile='';
    my $IssFile = "..\\svn_dynamics.iss";

    $ErrNoPathFile="ERROR: $IssFile not found. please, make sure that it's ";
    $ErrNoPathFile=$ErrNoPathFile . "where it\n should to be\n";

    $VarISPP = "#define " . $VarISPP;

    open (FH_ISSFILE, $IssFile) || die $ErrNoPathFile;
    while (<FH_ISSFILE>)
      {
			  chomp($_);

        if (/^$VarISPP/)
          {
              $_ =~ s/^$VarISPP//;
              $_ =~ s/^\s+//;
              $_ =~ s/\s+$//;
              $_ =~ s/^"//;
              $_ =~ s/"$//;
              $RetVal = $_;
              last;
          }
      }
    close (FH_ISSFILE);

    return $RetVal;
}

1;
