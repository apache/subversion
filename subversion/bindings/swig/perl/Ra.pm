package SVN::Ra;
use SVN::Base qw(Ra svn_ra_);
use SVN::Client ();
use strict;

my $ralib = init_ra_libs;

our $AUTOLOAD;

sub AUTOLOAD {
    my $class = ref($_[0]);
    $AUTOLOAD =~ s/^${class}::(SUPER::)?//;
    return if $AUTOLOAD =~ m/^[A-Z]/;

    my $self = shift;
    no strict 'refs';

    die "no such method $AUTOLOAD"
	unless $self->can("plugin_invoke_$AUTOLOAD");

    my @ret = &{"plugin_invoke_$AUTOLOAD"}(@{$self}{qw/ra session/}, @_,
					   $self->{pool});

    return $#ret ? bless [@ret], 'SVN::Ra::Reporter' : $ret[0];
}

sub new {
    my $class = shift;
    my $self = bless {}, $class;
    %$self = $#_ ? @_ : (url => $_[0]);

    $self->{auth} ||= SVN::Core::auth_open
	([ SVN::Client::get_username_provider ]);

    my $pool = $self->{pool} ||= SVN::Core::pool_create(undef);

    $self->{ra} = get_ra_library ($ralib, $self->{url});
    my $callback = 'SVN::Ra::Callbacks';

    # custom callback namespace
    if ($self->{callback} && !ref($self->{callback})) {
	$callback = $self->{callback};
	undef $self->{callback};
    }
    $self->{callback} ||= $callback->new(auth => $self->{auth},
						  pool => $pool),

    $self->{session} = plugin_invoke_open
	($self->{ra}, $self->{url}, $self->{callback},
	 $self->{config} || {}, $pool);

    return $self;
}

sub DESTROY {

}

package SVN::Ra::Reporter;
use SVN::Base qw(Ra svn_ra_reporter_);
use strict;

our $AUTOLOAD;
sub AUTOLOAD {
    my $class = ref($_[0]);
    $AUTOLOAD =~ s/^${class}::(SUPER::)?//;
    return if $AUTOLOAD =~ m/^[A-Z]/;

    my $self = shift;
    no strict 'refs';

    die "no such method $AUTOLOAD"
	unless $self->can("invoke_$AUTOLOAD");

    &{"invoke_$AUTOLOAD"}(@$self, @_);
}

package SVN::Ra::Callbacks;
require SVN::Core;

sub new {
    my $class = shift;
    my $self = bless {}, $class;
    %$self = @_;
    return $self;
}

sub open_tmp_file {
    my $self = shift;
    my ($fd, $name) = SVN::Core::io_open_unique_file
	('/tmp/foobar', 'tmp', 1, $self->{pool});
    return $fd;
}

sub get_wc_prop {
    return undef;
}

1;
