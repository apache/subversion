#!/usr/bin/perl

# Terribly ugly hack of a script to verify the signatures on the release
# tarballs and produce the list of who signed them in the format we use for
# the announcements.
#
# To use just run it in the directory with the signatures and tarballs and
# pass the version of subversion you want to check.  It assumes gpg is on
# you path, if it isn't you should fix that. :D
#
# Script will die if any gpg process returns an error.

my $version = $ARGV[0];
my @extensions = qw(tar.bz2 tar.gz zip);
my %good_sigs;

foreach my $extension (@extensions) {
  $filename = "subversion-$version.$extension.asc";
	my $gpg_output = `gpg --logger-fd 1 --verify $filename`;
  if ($? >> 8 ) {
	  # gpg exited with a non zero exit value, die with an error 
	  print $gpg_output;
	  die "BAD SIGNATURE in $filename";
	}
	foreach my $line (split /\n/, $gpg_output) {
		# Extract the keyid from the GPG output.
		my ($keyid) = $line =~ /^gpg: Signature made .*? using \w+ key ID (\w+)/;
		if (defined($keyid)) {
			# Put the resulting key in a hash to remove duplicates.
      $good_sigs{$keyid}++; 
		}
	}
}

foreach my $keyid (keys %good_sigs) {
	my $gpg_output = `gpg --fingerprint $keyid`;
	if ($? >> 8 ) {
	  # gpg exited with a non zero exit value, die with an error 
		print $gpg_output;
		die "UNABLE TO GET FINGERPRINT FOR $keyid";
	}
	my ($long_keyid, $fingerprint, $null, $name) = $gpg_output =~ /^pub\s+(\w+\/\w+)[^\n]*\n\s+Key\sfingerprint\s=((\s+[0-9A-F]{4}){10})\nuid\s+([^<\(]+)\s/;
	unless (defined($long_keyid) && defined($name) && defined($fingerprint)) {
		# Hmm some value didn't get filled in, error out.
		die "Empty value, possible error in gpg output parsing.";
	}
	print <<"EOL";
   $name [$long_keyid] with fingerprint:
   $fingerprint
EOL
}
