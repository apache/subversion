package SVN::Fs;
use SVN::Base qw(Fs svn_fs_);

package _p_svn_fs_t;

our @methods = qw/youngest_rev revision_root revision_prop revision_proplist
		 change_rev_prop list_transactions open_txn begin_txn/;

for (@methods) {
    *{$_} = *{"SVN::Fs::$_"};
}

package _p_svn_fs_root_t;

our @methods = qw/apply_textdelta apply_text change_node_prop
		 check_path close_root copied_from copy
		 dir_entries delete delete_tree file_contents
		 file_length file_md5_checksum is_dir is_file
		 is_revision_root is_txn_root make_dir make_file
		 node_created_rev node_history node_id node_prop
		 node_proplist paths_changed rename revision_link
		 revision_root_revision/;

*fs = *{"SVN::Fs::root_fs"};

for (@methods) {
    *{$_} = *{"SVN::Fs::$_"};
}

package _p_svn_fs_history_t;
use SVN::Base qw/Fs svn_fs_history_/;

package _p_svn_fs_txn_t;
use SVN::Base qw/Fs svn_fs_txn_/;

*close = *SVN::Fs::close_txn;
*commit = *SVN::Fs::commit_txn;

package _p_svn_fs_dirent_t;
use SVN::Base qw(Fs svn_fs_dirent_t_);

1;
