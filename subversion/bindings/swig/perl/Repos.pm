package SVN::Repos;
use SVN::Base qw(Repos svn_repos_);

package _p_svn_repos_t;

my @methods = qw/fs get_logs get_commit_editor/;

for (@methods) {
    *{$_} = *{"SVN::Repos::$_"};
}

package _p_svn_log_changed_path_t;
use SVN::Base qw(Core svn_log_changed_path_t_);



1;
