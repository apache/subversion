package SVN::Core;
use SVN::Base qw(Core svn_);
$VERSION = "$VER_MAJOR.$VER_MINOR.$VER_MICRO";
use strict;

=head1 NAME

SVN::Core - Core module of the subversion perl bindings

=head1 SYNOPSIS

    require SVN::Core; # does apr_initialize and cleanup for you

    # create a root pool and set it as default pool for later use
    my $pool = SVN::Pool->new_default;

    sub something {
        # create a subpool of the current default pool
        my $pool = SVN::Pool->new_default_sub;
	# some svn operations...

	# $pool gets destroyed and the previous default pool
	# is restored when $pool's lexical scope ends
    }

    # svn_stream_t as native perl io handle
    my $stream = $txn->root->apply_text('trunk/filea', undef);
    print $stream $text;
    close $stream;

    # native perl io handle as svn_stream_t
    SVN::Repos::dump_fs($repos, \*STDOUT, \*STDERR,
                        0, $repos->fs->youngest_rev, 0);

=head1 DESCRIPTION

SVN::Core implements higher level functions of fundamental subversion
functions.

=head1 FUNCTIONS

=over 4

=cut

BEGIN {
    SVN::_Core::apr_initialize;

}

END {
    SVN::_Core::apr_terminate;
}

=item SVN::Core::auth_open([auth provider array]);

Takes a reference to an array of authentication providers
and returns an auth_baton.  If you use prompt providers
you can not use this function, but need to use the 
auth_open_helper.

=item SVN::Core::auth_open_helper([auth provider array);

Prompt providers return two values instead of one.  The
2nd paramaeter is a reference to whatever was passed into
them as the callback.  auth_open_helper splits up these
arguments, passing the provider objects into auth_open
which gives it an auth_baton and putting the other
ones in an array.  The first return value of this
function is the auth_baton, the second is a reference
to an array containing the references to the callbacks.

These callback arrays should be stored in the object
the auth_baton is attached to.

=cut

sub auth_open_helper {
    my $args = shift;
    my (@auth_providers,@auth_callbacks);

    foreach my $arg (@{$args}) {
        if (ref($arg) eq '_p_svn_auth_provider_object_t') {
            push @auth_providers, $arg;
        } else {
            push @auth_callbacks, $arg;
        }
    }
    my $auth_baton = SVN::Core::auth_open(\@auth_providers);
    return ($auth_baton,\@auth_callbacks);
}

package _p_svn_stream_t;
use SVN::Base qw(Core svn_stream_);

package SVN::Stream;
use IO::Handle;
our @ISA = qw(IO::Handle);

=head1 OTHER OBJECTS

=over 4

=head2 svn_stream_t - SVN::Stream

You can use native perl io handles (including io globs) as
svn_stream_t in subversion functions. Returned svn_stream_t are also
translated into perl io handles, so you could access them with regular
print, read, etc.

Note that some functions takes a stream to read or write, while it
does not close it but still hold the reference to the handle. In this case
the handle won't be destroyed properly. You should always use correct
default pool before calling such functions.

=cut

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
    *$self->{svn_stream}->close
	if *$self->{svn_stream};
    undef *$self->{svn_stream};
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
    my ($buf, $eof) = *$self->{svn_stream}->readline ($/, *$self->{pool});
    return undef if $eof && !$buf;
    return $eof ? $buf : $buf.$/;
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
    elsif (ref $/) {
        return *$self->{svn_stream}->read (${$/}) || undef;
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

package _p_apr_pool_t;

my %WRAPPED;

sub default {
    my ($pool) = @_;
    my $pobj = SVN::Pool->_wrap ($$pool);
    $WRAPPED{$pool} = $pobj;
    $pobj->default;
}

sub DESTROY {
    my ($pool) = @_;
    delete $WRAPPED{$pool};
}

package SVN::Pool;
use SVN::Base qw/Core svn_pool_/;

=back

=over 4

=head2 svn_pool_t - SVN::Pool

The perl bindings significantly simplify the usage of pools, without
making them not manually adjustable.

Functions requiring pool as the last argument (which are, almost all
of the subversion functions), the pool is optionally. the default pool
is used if it is omitted. If default pool is not set, a new root pool
will be created and set as default automatically when the first
function requiring a default pool is called.

For callback functions providing pool to your subroutine, you could
also use $pool->default to make it the default pool in the scope.

=head3 Methods

=over

=item new ([$parent])

Create a new pool. The pool is a root pool if $parent is not supplied.

=item new_default ([$parent])

Create a new pool. The pool is a root pool if $parent is not supplied.
Set the new pool as default pool.

=item new_default_sub

Create a new subpool of the current default pool, and set the
resulting pool as new default pool.

=item clear

Clear the pool.

=item destroy

Destroy the pool. if the pool is the default pool, restore the
previous default pool as default. This is normally called
automatically when the SVN::Pool object is no longer used and
destroyed by the perl garbage collector.

=back

=cut

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

my %WRAPPOOL;

# Create a cloned _p_apr_pool_t pointing to the same apr_pool_t
# but on different address. this allows pools that are from C
# to have proper lifetime.
sub _wrap {
    my ($class, $rawpool) = @_;
    my $pool = \$rawpool;
    bless $pool, '_p_apr_pool_t';
    my $npool = \$pool;
    bless $npool, $class;
    $WRAPPOOL{$npool} = 1;
    $npool;
}

sub DESTROY {
    return if $globaldestroy;
    my $self = shift;
    if ($$self eq $SVN::_Core::current_pool) {
	$SVN::_Core::current_pool = pop @POOLSTACK;
    }
    if (exists $WRAPPOOL{$self}) {
        delete $WRAPPOOL{$self};
    }
    else {
        apr_pool_destroy ($$self)
    }
}

package _p_svn_log_changed_path_t;
use SVN::Base qw(Core svn_log_changed_path_t_);

=head2 svn_log_changed_path_t

=cut

package SVN::Node;
use SVN::Base qw(Core svn_node_);

=head2 svn_node_kind_t - SVN::Node

=cut

package _p_svn_opt_revision_t;
use SVN::Base qw(Core svn_opt_revision_t_);

=head2 svn_opt_revision_t

=cut

package _p_svn_opt_revision_t_value;
use SVN::Base qw(Core svn_opt_revision_t_value_);

package _p_svn_config_t;
use SVN::Base qw(Core svn_config_);

package _p_svn_dirent_t;
use SVN::Base qw(Core svn_dirent_t_);

=head2 svn_config_t

=cut

=head1 AUTHORS

Chia-liang Kao E<lt>clkao@clkao.orgE<gt>

=head1 COPYRIGHT

Copyright (c) 2003 CollabNet.  All rights reserved.

This software is licensed as described in the file COPYING, which you
should have received as part of this distribution.  The terms are also
available at http://subversion.tigris.org/license-1.html.  If newer
versions of this license are posted there, you may use a newer version
instead, at your option.

This software consists of voluntary contributions made by many
individuals.  For exact contribution history, see the revision history
and logs, available at http://subversion.tigris.org/.

=cut

1;
