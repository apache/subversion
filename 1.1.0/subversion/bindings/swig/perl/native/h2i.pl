# $Revision: #7 $ $Change: 7549 $ $DateTime: 2003/08/15 10:56:21 $
# extract member function from c header as invoker functions
# by Autrijus Tang

use 5.008;

my $file = shift or die "Usage: $^X $0 svn_ra.h svn_ra_reporter_t\n";
my $lines = '';

while (my $s = shift) {
    my $out = '';
    my $n = uc($s); $n =~ s/_T$//; $n =~ s/.*_//;
    my $prefix = $s; chop $prefix;
    my ($rv, $func, $args);

    open FILE, $file or die $!;
    my $temp = join('', <FILE>);
    $temp =~ s/^(typedef struct)(\n\{[^}]+\}\s+(\w+);)/$1 $3$2/ms;
    close FILE;
    open FILE, '<', \$temp;
    while (<FILE>) {
	next if 1 .. /^typedef struct $s/;
	next if /^{$/;
	next if m{^\s*/\*} .. m{\*/\s*$};
	last if /^\s*}\s+/;
	$lines .= $_;
	next unless /^\s*$/;
	(($lines = ''), next) unless $lines =~ /\s*(\w+)\s*\W+(\w+)\W+\(([^)]+)/; # only care about functions
	($rv, $func, $args) = ($1, $2, $3);

	print "$rv *${prefix}invoke_$func (\n";
	print "    $s *\L$n\E,\n";
	print join(",\n", map "    $_", split(/\s*,\s*/, $args));
	print "\n);\n\n";

	$out .= "$rv *
${prefix}invoke_${func} (const $s *\L$n\E, ${args})
";

	$args = join(
	    ', ',
	    map { s/^.*?(\w+)$/$1/; $_ }
	    split(/\s*,\s*/, $args)
	);
	$out .= "{ return \L${n}\E->${func} (${args}); }

";

	$lines = '';
    }

    print "%{\n";
    print $out;
    print "%}\n\n";
}

