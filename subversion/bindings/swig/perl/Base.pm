package SVN::Base;

sub import {
    my (undef, $pkg, $prefix) = @_;
    unless (defined %{"SVN::_${pkg}::"}) {
	@{"SVN::_${pkg}::ISA"} = qw(DynaLoader);
	eval qq'
package SVN::_$pkg;
require DynaLoader;
bootstrap SVN::_$pkg;
1;
    ' or die $@;
    };

    my $caller = caller(0);

    for (keys %{"SVN::_${pkg}::"}) {
	my $name = $_;
	next unless s/^$prefix//i;

	# insert the accessor
	if (m/(.*)_get$/) {
	    my $member = $1;
	    *{"${caller}::$1"} = sub {
		&{"SVN::_${pkg}::${prefix}${member}_".
		      (@_ > 1 ? 'set' : 'get')} (@_)
		  }
	}
	elsif (m/(.*)_set$/) {
	}
	else {
	    *{"${caller}::$_"} = ${"SVN::_${pkg}::"}{$name};
	}
    }

}

1;
