use strict;

for my $file qw( Client.pm Core.pm Delta.pm Fs.pm Ra.pm Repos.pm Wc.pm ) {
	system("$^X -Mblib scripts/fast_svnbase.pl $file > blib/lib/SVN/${file}c");
}
