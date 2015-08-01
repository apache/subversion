#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
# This is a listing of all cmdline tests currently known to check for proper
# tree-conflicts handling. You can use this file as a shell script, just
# go to your subversion/tests/cmdline directory and run this file.

./update_tests.py "$@" 33 34 46:54
./switch_tests.py "$@" 31:35
./merge_tests.py "$@" 101:103 111:121 128:129
./stat_tests.py "$@" 31
./info_tests.py "$@" 1
./revert_tests.py "$@" 19
./commit_tests.py "$@" 59:60
./tree_conflict_tests.py "$@"
./checkout_tests.py "$@" 13

