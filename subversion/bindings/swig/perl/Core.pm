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

=cut

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

=head2 svn_stream_t - SVN::Stream

You can use native perl io handles (including io globs) as
svn_stream_t in subversion functions. Returned svn_stream_t are also
translated into perl io handles, so you could access them with regular
print, read, etc.

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

=head2 svn_pool_t - SVN::Pool

The perl bindings significantly simplify the usage of pools, without
making them not manually adjustable.

Functions requiring pool as the last argument (which are, almost all
of the subversion functions), the pool is optionally. the default pool
is used if it is omitted. If default pool is not set, a new root pool
will be created and set as default automatically when the first
function requiring a default pool is called.

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

sub DESTROY {
    return if $globaldestroy;
    my $self = shift;
    if ($$self eq $SVN::_Core::current_pool) {
	$SVN::_Core::current_pool = pop @POOLSTACK;
    }
    apr_pool_destroy ($$self);
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
