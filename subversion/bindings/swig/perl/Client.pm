package SVN::Client;
use SVN::Base qw(Client svn_client_);

=head1 NAME

SVN::Client - Subversion client functions

=head1 SYNOPSIS

    require SVN::Core;
    require SVN::Client;
    my $ctx = SVN::_Client::new_svn_client_ctx_t ();

    $ctx->auth_baton (SVN::Core::auth_open
          ([SVN::Client::get_simple_provider,
            SVN::Client::get_username_provider]));

    SVN::Client::cat (\*STDOUT,
                      'http://svn.collab.net/repos/svn/trunk/README',
                      'HEAD', $ctx);

=head1 DESCRIPTION

SVN::Client wraps the highest level of functions provided by
subversion to accomplish specific tasks. Consult the svn_client.h
section in the Subversion API. the svn_client_ctx_t object could be
obtained as showed above in SYNOPSIS.

The author does not have much incentive to support SVN::Client, since
the behavior is really just like what you can get from the
command-line client - svn.

=cut

package _p_svn_client_ctx_t;
use SVN::Base qw(Client svn_client_ctx_t_);

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
