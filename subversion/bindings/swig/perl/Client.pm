use SVN::Core;

package SVN::Client;
use SVN::Base(qw(Client svn_client_ checkout update switch add mkdir delete
                 commit status log blame diff merge cleanup relocate
                 revert resolved copy move revprop_set propset
                 proplist revvprop_list export ls cat import)); 

=head1 NAME

SVN::Client - Subversion client functions

=head1 SYNOPSIS

    use SVN::Client;
    my $ctx = new SVN::Client(
              auth => [SVN::Client::get_simple_provider(),
              SVN::Client::get_simple_prompt_provider(\&simple_prompt,2),
              SVN::Client::get_username_provider()]
              );

    $ctx->cat (\*STDOUT, 'http://svn.collab.net/repos/svn/trunk/README', 
               'HEAD');

    sub simple_prompt {
      my $cred = shift;
      my $realm = shift;
      my $default_username = shift;
      my $may_save = shift;
      my $pool = shift;

      print "Enter authentication info for realm: $realm\n";
      print "Username: ";
      my $username = <>;
      chop($username);
      $cred->username($username);
      print "Password: ";
      my $password = <>;
      chomp($password);
      $cred->password($password);
    }

=head1 DESCRIPTION

SVN::Client wraps the highest level of functions provided by
subversion to accomplish specific tasks in an object oriented API.
Methods are similar to the functions provided by the C API and
as such the documentation for it may be helpful in understanding
this interface.

There are a few notable differences from the C API.  Most C function
calls take a svn_client_ctx_t pointer as the next to last parameter.
The perl method calls take a SVN::Client object as the first parameter.
This allows method call invocation of the methods to be possible.

Many of the C API calls also take a apr_pool_t pointer as their last
argument.  The Perl bindings generally deal with this for you and
you do not need to pass a pool parameter.  However, you may still
pass a pool parameter as the last parameter to override the automatic
handling of this for you.

Users of this interface should not directly manipulate the underlying hash
values but should use the respective attribute methods.  Many of these
attribute methods do other things, especially when setting an attribute,
besides simply manipulating the value in the hash.

=head1 METHODS

The following methods are available:

=over 4

=item $ctx = SVN::Client->new( %options );

This class method constructs a new C<SVN::Client> object and returns
a reference to it.

Key/value pair arguments may be provided to set up the initial state
of the user agent.  The following methods correspond to attribute
methods described below:

    KEY                    DEFAULT
    ----------             ----------------------------------------
    config                 Hash containing the config from the 
                           default subversion config file location.

    auth                   auth_baton intiated to provide the
                           provider that read cached authentication
                           options from the subversion config only.

    pool                   A new pool is created for the context.

=cut

sub new
{
    my $class = shift;
    my $self = bless {}, $class;
    my %args = @_;

    $self->{'ctx'} = SVN::_Client::new_svn_client_ctx_t ();

    if (defined($args{'auth'}))
    {
        $self->auth($args{'auth'});
    } else {
        $self->auth([SVN::Client::get_username_provider(),
                     SVN::Client::get_simple_provider(),
                     SVN::Client::get_ssl_server_trust_file_provider(),
                     SVN::Client::get_ssl_client_cert_file_provider(),
                     SVN::Client::get_ssl_client_cert_pw_file_provider(),
                    ]);
    }

    {
        my $pool_type = ref($args{'pool'});
        if ($pool_type eq 'SVN::Pool' ||
            $pool_type eq '_p_apr_pool_t') 
        {
            $self->{'pool'} = $args{'pool'};
        } else {
            $self->{'pool'} = new SVN::Pool();
        }
    }

    # If we're passed a config use it, otherwise get the default
    # config.
    if (defined($args{'config'}))
    {
        if (ref($args{'config'}) eq 'HASH')
        {
            $self->config($args{'config'});
        }
    } else {
        $self->config(SVN::Core::config_get_config(undef));
    }
    return $self;
}

=item $ctx->cat(\*FILEHANDLE, path_or_url, revision, pool);

Outputs the content of the file identified by path_or_url and revision to the
FILEHANDLE.  FILEHANLDE is a reference to a filehandle.  revision should be a
number or 'HEAD'.  pool is an optional parameter.

=cut

# import methods into our name space and wrap them in a closure
# to support method calling style $ctx->log()
foreach my $function (qw(checkout update switch add mkdir delete commit
                       status log blame diff merge cleanup relocate
                       revert resolved copy move revprop_set propset
                       proplist revvprop_list export ls cat import))
{

    my $real_function = \&{"SVN::_Client::svn_client_$function"};
    *{"SVN::Client::$function"} = sub
    {
        # Allows import to work while not breaking use SVN::Client.
        if ($function eq 'import')
        {
            if (ref($_[$[]) ne 'SVN::Client')
            {
                return;
            }
        }

        my $self = shift;
        my @args;

        if (ref($_[$#_]) eq '_p_apr_pool_t')
        {
            # if we got a pool pased to us we need to
            # leave it off until we add the ctx first
            # so we push only the first arg to the next
            # to last arg.
            push @args, @_[$[ .. ($#_ - 1)];
            unless ($function eq 'propset')
            {
                # propset doesn't take a ctx argument
                push @args, $self->{'ctx'};
            }
            push @args, $_[$#_];
        } else {
            push @args, @_;
            unless ($function eq 'propset')
            {
                push @args,$self->{'ctx'};
            }
            if (defined($self->{'pool'}) && 
                ref($self->{'pool'}) eq '_p_apr_pool_t')
            {
                # allow the pool entry in the SVN::Client
                # object to override the default pool.
                push @args, $self->{'pool'};
            }
        }
        return $real_function->(@args);
    }
}

=head1 ATTRIBUTE METHODS

The following attribute methods are provided that allow you to set various
configuration or retrieve it.  They all take value(s) to set the attribute and
return the new value of the attribute or no paremeters which reuturns the
current value.

=item $ctx->auth(SVN::Client::get_username_provider());

Provides access the auth_baton in the svn_client_ctx_t attached to the
SVN::Client object.

This method will accept an array or array ref of values returned from the
authentication provider functions see L</"AUTHENTICATION PROVIDERS">.  Which it
will convert to an auth_baton for you.  This is the preferred method of setting
the auth_baton.

It will also accept a scalar that references a _p_svn_auth_baton_t such as
those returned from SVN::Core::auth_open and SVN::Core::auth_open_helper.

=cut

sub auth
{
    my $self = shift;
    my $args;
    if (scalar(@_) == 0)
    {
        return $self->{'ctx'}->auth_baton();
    } elsif (scalar(@_) > 1) {
        $args = \@_;
    } else {
        $args = shift;
        if (ref($args) eq '_p_svn_auth_baton_t')
        {
            # 1 arg as an auth_baton so just set
            # the baton.
            $self->{'ctx'}->auth_baton($args);
            return $self->{'ctx'}->auth_baton();
        }
    }

    my ($auth_baton,$callbacks) = SVN::Core::auth_open_helper($args);
    $self->{'auth_provider_callbacks'} = $callbacks;
    $self->{'ctx'}->auth_baton($auth_baton);
    return $self->{'ctx'}->auth_baton();
}

=item $ctx->pool(new SVN::Pool);

Method that sets or gets the pool that is passed to method calls requiring a
pool but that you didn't pass one.

See L<SVN::Core> for more information about how pools are managed
in this interface.

=cut

sub pool
{
    my $self = shift;

    if (scalar(@_) == 0)
    {
        $self->{'pool'};
    } else {
        return $self->{'pool'} = shift;
    }
}

=item $ctx->config(SVN::Core::config_get_config(undef));

Method that allows access to the config member of the svn_client_ctx_t.
Accepts a perl hash to set, which is what functions like
SVN::Core:config_get_config() will return.

It will return a _p_arp_hash_t scalar.  THis is a temporary
situation.  The return value is not particular useful.  In
the future, this value will be tied to the actual hash used
by the C API.

=cut

sub config
{
    my $self = shift;
    if (scalar(@_) == 0) {
        return $self->{'ctx'}->config();
    } else {
        $self->{'ctx'}->config(shift);
        return $self->{'ctx'}->config();
    }
}


=head1 AUTHENTICATION PROVIDERS

The following functions get authentication providers for you.
They come in two forms.  Standard or File versions, which look
for authentication information in the subversion configuration
directory that was previously cached, or Prompt versions which
call a subroutine to allow you to prompt the user for the
information.

The functions that return the provider objects for prompt style providers
take a reference to a perl subroutine to use for the callback.  The first
parameter each of these subroutines receive is a credential object.  The
subroutines return the response by setting members of that object.  Members
may be set like so: $cred->username("breser");  These functions and credential
objects always have a may_save member which specifies if the authentication
data will be cached.

The providers are as follows:

        NAME                WHAT IT HANDLES
        ----------------    ----------------------------------------
        simple              username and password pairs

        username            username only

        ssl_server_trust    server certificates and failures
                            authenticating them

        ssl_client_cert     client side certificate files

        ssl_client_cert_pw  password for a client side certificate file.


=over 4

=item SVN::Client::get_simple_provider

Returns a simple provider that returns information from previously cached
sessions.  Takes no parameters or one pool parameter.

=item SVN::Client::get_simple_prompt_provider

Returns a simple provider that prompts the user via a callback. Takes two or
three parameters, the first is the callback subroutine, the 2nd is the number
of retries to allow, the 3rd is optionally a pool.  The subroutine gets called
with the following parameters.  A svn_auth_cred_simple object, a realm string,
a default username, may_save, and a pool.  The svn_auth_cred_simple has the
following members: username, password, and may_save.

=item SVN::Client::get_username_provider

Returns a username provider that returns information from a previously cached
sessions.  Takes no parameters or one pool parameter.

=item SVN::Client::get_username_prompt_provider

Returns a username provider that prompts the user via a callback. Takes two or
three parameters, the first is the callback subroutine, the 2nd is the number
of retries to allow, the 3rd is optionally a pool.  The subroutine gets called
with the following parameters.  A svn_auth_cred_username object, a realm
string, a default username, may_save, and a pool.  The svn_auth_cred_username
has the following members: username and may_save.

=item SVN::Client::get_ssl_server_trust_file_provider

Returns a server trust provider that returns infromation from previously
cached sessions.  Takes no parameters or optionally a pool parameter.

=item SVN::Client::get_ssl_server_trust_prompt_provider

Returns a server trust  provider that prompts the user via a callback. Takes
one or two parameters the callback subroutine and optionally a pool parameter.
The subroutine gets called with the following parameters.  A
svn_auth_cred_ssl_server_trust object, a realm string, an integer specifiying
how the certificate failed authentication, a cert info object, may_save, and a
pool.  The svn_auth_cred_ssl_server_trust object has the following members:
may_save and accepted_failures.  The svn_ssl_cert_info object has the following
members (and behaves just like cred objects though you can't modify it):
hostname, fingerprint, valid_from, valid_until, issuer_dname, ascii_cert.

The masks used for determaning the failures are in SVN::_Core and are named:

$SVN::_Core::SVN_AUTH_SSL_NOTYETVALID
$SVN::_Core::SVN_AUTH_SSL_EXPIRED
$SVN::_Core::SVN_AUTH_SSL_CNMISMATCH
$SVN::_Core::SVN_AUTH_SSL_UNKNOWNCA
$SVN::_Core::SVN_AUTH_SSL_OTHER

You reply by setting the accepted_failures of the cred object with an integer 
of the values for what you want to accept bitwise anded together.

=item SVN::Client::get_ssl_cert_file_provider

Returns a client certificate provider that returns infromation from previously
cached sessions.  Takes no parameters or optionally a pool parameter.

=item SVN::Client::get_ssl_cert_prompt_provider

Returns a client certificate provider that prompts the user via a callback.
Takes two or three parameters, the first is the callback subroutine, the 2nd is
the number of retries to allow, the 3rd is optionally a pool parameter.  The
subroutine gets called with the following parameters.  A
svn_auth_cred_ssl_client_cert object, a realm string, may_save, and a pool.
The svn_auth_cred_ssl_client_cert the following members: cert_file and
may_save.

=item SVN::Client::get_ssl_cert_pw_file_provider

Returns a client certificate password provider that returns infromation from
previously cached sessions.  Takes no parameters or optionally a pool
paramater.

=item SVN::Client::get_ssl_cert_pw_prompt_provider

Returns a client certificate passowrd provider that prompts the user via a
callback. Takes two or three parameters, the first is the callback subroutine,
the 2nd is the number of retries to allow, the 3rd is optionally a pool
parameter.  The subroutine gets called with the following parameters.  A
svn_auth_cred_ssl_client_cert_pw object, a realm string, may_save, and a pool.
The svn_auth_cred_ssl_client_cert_pw has the following members: password and
may_save.

=cut

package _p_svn_client_commit_info_t;
use SVN::Base qw(Client svn_client_commit_info_t_);

package _p_svn_client_commit_item_t;
use SVN::Base qw(Client svn_client_commit_item_t_);

package _p_svn_client_ctx_t;
use SVN::Base qw(Client svn_client_ctx_t_);

package _p_svn_client_proplist_item_t;
use SVN::Base qw(Client svn_client_proplist_item_t_);

=head1 TODO

* Complete documentation 

* Support for the notify callback.

* Support for the log_msg callback.

* Better support for the config.

* More unit tests.

=head1 AUTHORS

Chia-liang Kao E<lt>clkao@clkao.orgE<gt>
Ben Reser E<lt>ben@reser.orgE<gt>

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
