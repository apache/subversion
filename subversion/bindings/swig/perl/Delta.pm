package SVN::Delta;
use SVN::Base qw(Delta svn_delta_);

package _p_svn_txdelta_op_t;
use SVN::Base qw(Delta svn_txdelta_op_t_);

package _p_svn_txdelta_window_t;
use SVN::Base qw(Delta svn_txdelta_window_t_);

package SVN::Delta::Editor;
use SVN::Base qw(Delta svn_delta_editor_);
*invoke_set_target_revision = *SVN::_Delta::svn_delta_editor_invoke_set_target_revision;
use strict;

sub new {
    my $class = shift;
    my $self = bless {}, $class;

    if (ref($_[0]) && $_[0]->isa('_p_svn_delta_editor_t')) {
	@{$self}{qw/_editor _baton/} = @_;
    }
    else {
	%$self = @_;
    }

    warn "debug" if $self->{_debug};

    return $self;
}

our $AUTOLOAD;

sub AUTOLOAD {
    warn "$AUTOLOAD: ".join(',',@_) if $_[0]->{_debug};
    return unless $_[0]->{_editor};
    my $class = ref($_[0]);
    $AUTOLOAD =~ s/^${class}::(SUPER::)?//;
    return if $AUTOLOAD =~ m/^[A-Z]/;

    my %ebaton = ( set_target_revision => 1,
		   open_root => 1,
		   close_edit => 1,
		   abort_edit => 1,
		 );

    my $self = shift;
    no strict 'refs';
    my @ret = &{"invoke_$AUTOLOAD"}($self->{_editor},
			  $ebaton{$AUTOLOAD} ? $self->{_baton} : (), @_);

    return $#ret == 0 ? $ret[0] : [@ret];
}

1;
