package SVN::Core;
use SVN::Base qw(Core svn_);

BEGIN {
    SVN::_Core::apr_initialize;

}

END {
    SVN::_Core::apr_terminate;
}

package _p_svn_stream_t;
use SVN::Base qw(Core svn_stream_);

package _p_svn_opt_revision_t;
use SVN::Base qw(Core svn_opt_revision_t_);

package _p_svn_opt_revision_t_value;
use SVN::Base qw(Core svn_opt_revision_t_value_);

package _p_svn_config_t;
use SVN::Base qw(Core svn_config_);

1;
