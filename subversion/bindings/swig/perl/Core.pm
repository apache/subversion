package SVN::Core;
use SVN::Base qw(Core svn_);

$VERSION = "$VER_MAJOR.$VER_MINOR.$VER_MICRO";

BEGIN {
    SVN::_Core::apr_initialize;

}

END {
    SVN::_Core::apr_terminate;
}

package _p_svn_stream_t;
use SVN::Base qw(Core svn_stream_);

package SVN::Stream;
use IO::Handle;
our @ISA = qw(IO::Handle);
use strict;

use Symbol ();

sub new
{
    my $class = shift;
    my $self = bless Symbol::gensym(), ref($class) || $class;
    tie *$self, $self;
    *$self->{svn_stream} = shift;
    $self;
}

sub svn_stream {
    my $self = shift;
    *$self->{svn_stream};
}

sub TIEHANDLE
{
    return $_[0] if ref($_[0]);
    my $class = shift;
    my $self = bless Symbol::gensym(), $class;
    *$self->{svn_stream} = shift;
    $self;
}

sub CLOSE
{
    my $self = shift;
    *$self->{svn_stream}->close;
}

sub GETC
{
    my $self = shift;
    my $buf;
    return $buf if $self->read($buf, 1);
    return undef;
}

sub print
{
    my $self = shift;
    $self->WRITE ($_[0], length ($_[0]));
}

sub PRINT
{
    my $self = shift;
    if (defined $\) {
        if (defined $,) {
	    $self->print(join($,, @_).$\);
        } else {
            $self->print(join("",@_).$\);
        }
    } else {
        if (defined $,) {
            $self->print(join($,, @_));
        } else {
            $self->print(join("",@_));
        }
    }
}

sub PRINTF
{
    my $self = shift;
    my $fmt = shift;
    $self->print(sprintf($fmt, @_));
}

sub getline
{
    my $self = shift;
    *$self->{pool} ||= SVN::Core::pool_create (undef);
    my $buf = *$self->{svn_stream}->readline (*$self->{pool});
    return defined $buf ? $buf."\n" : undef;
}

sub getlines
{
    die "getlines() called in scalar context\n" unless wantarray;
    my $self = shift;
    my($line, @lines);
    push @lines, $line while defined($line = $self->getline);
    return @lines;
}

sub READLINE
{
    my $self = shift;
    unless (defined $/) {
	my $buf = '';
	while (my $chunk = *$self->{svn_stream}->read
	       ($SVN::Core::STREAM_CHUNK_SIZE)) {
	    $buf .= $chunk;
	}
	return $buf;
    }
    return wantarray ? $self->getlines : $self->getline;
}

sub READ {
    my $self = shift;
    my $len = $_[1];
    if (@_ > 2) { # read offset
        substr($_[0],$_[2]) = *$self->{svn_stream}->read ($len);
    } else {
        $_[0] = *$self->{svn_stream}->read ($len);
    }
    return $len;
}

sub WRITE {
    my $self = shift;
    my $slen = length($_[0]);
    my $len = $slen;
    my $off = 0;

    if (@_ > 1) {
        $len = $_[1] if $_[1] < $len;
        if (@_ > 2) {
            $off = $_[2] || 0;
            die "Offset outside string" if $off > $slen;
            if ($off < 0) {
                $off += $slen;
                die "Offset outside string" if $off < 0;
            }
            my $rem = $slen - $off;
            $len = $rem if $rem < $len;
        }
	*$self->{svn_stream}->write (substr ($_[0], $off, $len));
    }
    return $len;
}

*close = \&CLOSE;

sub FILENO {
    return undef;   # XXX perlfunc says this means the file is closed
}

sub DESTROY {
    my $self = shift;
    $self->close;
}

package SVN::Pool;
use SVN::Base qw/Core svn_pool_/;
no strict 'refs';
*{"apr_pool_$_"} = *{"SVN::_Core::apr_pool_$_"}
    for qw/clear destroy/;

my @POOLSTACK;

sub new {
    my ($class, $parent) = @_;
    $parent = $$parent if ref ($parent) eq 'SVN::Pool';
    my $self = bless \create ($parent), $class;
    return $self;
}

sub new_default_sub {
    my $parent = ref ($_[0]) ? ${+shift} : $SVN::_Core::current_pool;
    my $self = SVN::Pool->new_default ($parent);
    return $self;
}

sub new_default {
    my $self = new(@_);
    $self->default;
    return $self;
}

sub default {
    my $self = shift;
    push @POOLSTACK, $SVN::_Core::current_pool
	unless $$SVN::_Core::current_pool == 0;
    $SVN::_Core::current_pool = $$self;
}

sub clear {
    my $self = shift;
    apr_pool_clear ($$self);
}

my $globaldestroy;

END {
    $globaldestroy = 1;
}

sub DESTROY {
    return if $globaldestroy;
    my $self = shift;
    if ($$self eq $SVN::_Core::current_pool) {
	$SVN::_Core::current_pool = pop @POOLSTACK;
    }
    apr_pool_destroy ($$self);
}

package _p_svn_opt_revision_t;
use SVN::Base qw(Core svn_opt_revision_t_);

package _p_svn_opt_revision_t_value;
use SVN::Base qw(Core svn_opt_revision_t_value_);

package _p_svn_config_t;
use SVN::Base qw(Core svn_config_);

1;
