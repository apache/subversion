package SVN::Fs;
use SVN::Base qw(Fs svn_fs_);

package _p_svn_fs_t;

my @methods = qw/youngest_rev revision_root revision_prop revision_proplist
		 change_rev_prop list_transactions open_txn/;

for (@methods) {
    *{$_} = *{"SVN::Fs::$_"};
}

package _p_svn_fs_txn_t;
use SVN::Base qw/Fs svn_fs_txn_/;

package _p_svn_fs_dirent_t;
use SVN::Base qw(Fs svn_fs_dirent_t_);

1;
