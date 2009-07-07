#!perl
##########################################################################
# FILE       write_email
# PURPOSE    Write the body of the email to send out
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
use Cwd;
use Win32;
use Digest::MD5;
use Digest::SHA1;
require 'cmn.pl';

sub Main;

Main;

sub Main
{
	my $SvnVersion=&cmn_ValuePathfile('svn_version.ini', 'svn_version');

	#get the MD5 checksum  

	my $binfile = '..\BuildSubversion\bin\Release\en-us\\'.'Setup-Subversion-'.$SvnVersion.'.msi';
	
	open(FILE, $binfile) or die "Can't open '$binfile': $!";
	binmode(FILE);

	my $md5 = Digest::MD5->new;
	while (<FILE>) {
		$md5->add($_);
	}
	close(FILE);
	
	#get the SHA1 checksum
	open(FILESHA1, $binfile) or die "Can't open '$binfile': $!";
	binmode(FILESHA1);

	my $sha1 = Digest::SHA1->new;
	while (<FILESHA1>) {
		$sha1->add($_);
	}
	close(FILESHA1);
    
	my $EmailFile = '..\BuildSubversion\bin\Release\en-us\email_content.txt';
	open(MYFILE, '>'.$EmailFile);
	print MYFILE "I am happy to announce the release of the Subversion ".$SvnVersion." win32 installer based on D.J. Heap's win32 binaries.\n\n";

	print MYFILE "Note, that this installer includes apache 2.2.x binaries (2.2.9 or higher is required within the 2.2.x series).\n\n";
	print MYFILE "The installer may be downloaded from the win32 download area here (watch wrapping):\n\n";
	print MYFILE "**REPLACE: INSTALLER URL**\n\n";

	print MYFILE "MD5 checksum:\n";

	print MYFILE $md5->hexdigest, " *Setup-Subversion-".$SvnVersion.".msi\n\n"; 
 
	print MYFILE "SHA1 checksum:\n";

	print MYFILE $sha1->hexdigest, "  Setup-Subversion-".$SvnVersion.".msi\n\n"; 

	print MYFILE "PGP Signature:\n";
	print MYFILE "(watch wrapping)\n\n";
	print MYFILE "http://www.ebswift.com/Common/ASPCommon/Download/file_download.aspx?File=/subversion/Setup-Subversion-".$SvnVersion.".msi.sig\n\n";

	print MYFILE "Public PGP Key:\n";
	print MYFILE "http://www.ebswift.com/Common/ASPCommon/Download/file_download.aspx?File=/subversion/svnpubkey.asc\n\n\n";

	print MYFILE "Regards,\n\n";

	print MYFILE "Troy Simpson\n";
	
	close(MYFILE);

}

