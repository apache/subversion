#!perl -w
#!perl
##########################################################################
# FILE       gpgsign
# PURPOSE    Create the gnupg signature
# ====================================================================
#    Licensed to the Subversion Corporation (SVN Corp.) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The SVN Corp. licenses this file
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

