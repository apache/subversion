#!/usr/bin/perl
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

use strict;
use Test::More tests => 17;

# shut up about variables that are only used once.
# these come from constants and variables used
# by the bindings but not elsewhere in perl space.
no warnings 'once';

# TEST
use_ok('SVN::Core');
# TEST
use_ok('SVN::Wc');

my $external_desc = <<END;
http://svn.example.com/repos/project1 project1
^/repos/project2 "Project 2"
END

# Run parse_externals_description3()
# TEST
my ($externals) = SVN::Wc::parse_externals_description3("/fake/path",
                                                        $external_desc, 1);
isa_ok($externals, 'ARRAY', 'parse_externals_description3 returns array ref');

# Check the first member of the returned array.
# TEST
isa_ok($externals->[0], '_p_svn_wc_external_item2_t');
# TEST
ok($externals->[0]->target_dir(), 'project1');
# TEST
ok($externals->[0]->url(), 'http://svn.example.com/repos/project1');
# TEST
ok($externals->[0]->revision(), 'svn_opt_revision_t');
# TEST
ok($externals->[0]->revision()->kind(), $SVN::Core::svn_opt_revision_head);
# TEST
ok($externals->[0]->peg_revision(), 'svn_opt_revision_t');
# TEST
ok($externals->[0]->peg_revision()->kind(),
   $SVN::Core::svn_opt_revision_head);

# Check the second member
# TEST
isa_ok($externals->[0], '_p_svn_wc_external_item2_t');
# TEST
ok($externals->[0]->target_dir(), 'Project 2');
# TEST
ok($externals->[0]->url(), '^/repos/project2');
# TEST
ok($externals->[0]->revision(), 'svn_opt_revision_t');
# TEST
ok($externals->[0]->revision()->kind(), $SVN::Core::svn_opt_revision_head);
# TEST
ok($externals->[0]->peg_revision(), 'svn_opt_revision_t');
# TEST
ok($externals->[0]->peg_revision()->kind(),
   $SVN::Core::svn_opt_revision_head);

