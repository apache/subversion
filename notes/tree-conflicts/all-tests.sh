# This is a listing of all cmdline tests currently known to check for proper
# tree-conflicts handling. You can use this file as a shell script, just
# go to your subversion/tests/cmdline directory and run this file.

./update_tests.py "$@" 46:50
./switch_tests.py "$@" 31:35
./merge_tests.py "$@" 101:103 111:121
./stat_tests.py "$@" 31
./info_tests.py "$@" 1
./revert_tests.py "$@" 19
./commit_tests.py "$@" 59:60
./tree_conflict_tests.py "$@"

