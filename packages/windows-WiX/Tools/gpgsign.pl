#!perl -w
#!perl
##########################################################################
# FILE       gpgsign
# PURPOSE    Create the gnupg signature
# ====================================================================
# Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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
use warnings;
use Cwd;
use Win32;
use Win32::GUI();
require 'cmn.pl';

my $main = Win32::GUI::DialogBox->new(
	-name => 'Main',
	-text => 'GPG Signature',
	-width => 450,
       	-height => 80
);

my $passphrase = $main->AddTextfield(
	-name    => 'passphrase',
	-text    => '',
	-prompt => 'Please enter the passphrase:',
	-default => 1,    # Give button darker border
	-ok      => 1,    # press 'Return' to click this button
	-width   => 200,
	-height  => 20,
	-left    => 160,
	-top     => $main->ScaleHeight() - 30,
);

$main->AddButton(
	-name    => 'Default',
	-text    => 'Ok',
	-default => 0,    # Give button darker border
	-ok      => 1,    # press 'Return' to click this button
	-width   => 60,
	-height  => 20,
	-left    => $main->ScaleWidth() - 70,
	-top     => $main->ScaleHeight() - 30,
);

$main->Show();
Win32::GUI::Dialog();
exit(0);

sub Main_Terminate {
	return -1;
}

sub Default_Click {
	my $SvnVersion=&cmn_ValuePathfile('svn_version.ini', 'svn_version');
	#WARNING: hard coded path to gnupg
	my $gpg = 'echo '.$passphrase->Text().'|c:\gnupg\gpg --logger-file sign.log --verbose --yes --passphrase-fd 0 -b ..\BuildSubversion\bin\Release\en-us\Setup-Subversion-'.$SvnVersion.'.msi';

	my @info = qx($gpg);

	return -1;
}

