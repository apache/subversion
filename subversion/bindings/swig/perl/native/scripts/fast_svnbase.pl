use strict;

=head1 NAME

fast_svnbase.pl

=head1 DESCRIPTION

This script extracts the SVN::Base import magic and make them static
in the SVN::* modules to avoid BEGIN time overload.

You should put the output of SVN/Fs.pm to SVN/Fs.pmc, and Perl will be
loading this version for you.

=head1 TODO

Set the VERSION to something sensible.

Hook up with build system.

=cut


local $/;
my $buf = <>;

$buf =~ s/use\s+SVN::Base\s+.*?\((.*?)\);/fast_svnbase($1)/sgme;

print $buf;

sub fast_svnbase {
    my $line = shift;
    my ($what, $prefix, @exclude) = split /\s+/, $line;

    no strict 'refs';
    my $boot = exists ${"SVN::_$what".'::'}{ISA} ? '' : qq'
{
  use XSLoader;
  package SVN::_$what;
  XSLoader::load SVN::_$what;
}

';
    eval $boot;
 return "BEGIN ".$boot._compile_import($what, $prefix, @exclude);

}

sub _compile_import {
    my ($pkg, $prefix, @ignore) = @_;
    my $prefix_re = qr/(?i:$prefix)/;
    my $ignore_re = join('|', @ignore);
    warn "Compiling $pkg / $prefix\n";
    my @subs;
    no strict 'refs';
    for (keys %{"SVN::_${pkg}::"}) {
	my $name = $_;
	next unless s/^$prefix_re//;
	next if $ignore_re && m/$ignore_re/;

	# insert the accessor
	if (m/(.*)_get$/) {
	    my $member = $1;
	    push @subs, qq!*$member = sub { &{"SVN::_${pkg}::${prefix}${member}_".
		      (\@_ > 1 ? 'set' : 'get')}(\@_)} !;
	}
	elsif (m/(.*)_set$/) {
	}
	else {
	    push @subs, qq!*$_ = \$SVN::_${pkg}::{$name}!;
	}
    }
    return "{ no strict 'refs';\n  ". join(";\n  ", @subs, '')."}\n";
}
