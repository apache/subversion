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
use warnings;

use Test::More tests => 8;
use File::Temp qw(tempdir);
use File::Path qw(rmtree);
use File::Spec;
use POSIX qw(locale_h);

use SVN::Core;
use SVN::Repos;
use SVN::Fs;
use SVN::Delta;

setlocale(LC_ALL, "C");

my $repospath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 1);

my $repos;

# TEST
ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;

sub committed {
    diag "committed ".join(',',@_);
}

my $editor = SVN::Delta::Editor->
    new(SVN::Repos::get_commit_editor($repos, "file://$repospath",
                                      '/', 'root', 'FOO', \&committed));

my $rootbaton = $editor->open_root(0);

my $dirbaton = $editor->add_directory('trunk', $rootbaton, undef, 0);

my $fbaton = $editor->add_file('trunk/filea', $dirbaton, undef, -1);

my $ret = $editor->apply_textdelta($fbaton, undef);

SVN::TxDelta::send_string("FILEA CONTENT", @$ret);

$editor->close_edit();

# TEST
cmp_ok($fs->youngest_rev, '==', 1);
{
$editor = SVN::Delta::Editor->
    new(SVN::Repos::get_commit_editor($repos, "file://$repospath",
                                      '/', 'root', 'FOO', \&committed));
my $rootbaton = $editor->open_root(1);

my $dirbaton = $editor->add_directory('tags', $rootbaton, undef, 1);
my $subdirbaton = $editor->add_directory('tags/foo', $dirbaton,
                                         "file://$repospath/trunk", 1);

$editor->close_edit();
}
# TEST
cmp_ok($fs->youngest_rev, '==', 2);

my @history;

SVN::Repos::history($fs, 'tags/foo/filea',
                    sub {push @history, [@_[0,1]]}, 0, 2, 1);

# TEST
is_deeply(\@history, [['/tags/foo/filea',2],['/trunk/filea',1]],
          'repos_history');

{
my $pool = SVN::Pool->new_default;
my $something = bless {}, 'something';
$editor = SVN::Delta::Editor->
    new(SVN::Repos::get_commit_editor($repos, "file://$repospath",
                                      '/', 'root', 'FOO', sub {committed(@_);
                                                               $something;
                                                          }));

my $rootbaton = $editor->open_root(2);
$editor->delete_entry('tags', 2, $rootbaton);

$editor->close_edit();
}
# TEST
ok($main::something_destroyed, 'callback properly destroyed');

# TEST
cmp_ok($fs->youngest_rev, '==', 3);

open my $dump_fh, ">", File::Spec->devnull or die "open file sink: $!";

my $feedback;
open my $feedback_fh, ">", \$feedback or die "open string: $!";

my $cancel_cb_called = 0;
$repos->dump_fs2($dump_fh, $feedback_fh,
                 0, $SVN::Core::INVALID_REVNUM,     # start_rev, end_rev
                 0, 0,                              # incremental, deltify
                 sub { $cancel_cb_called++; 0 });
# TEST
ok($cancel_cb_called, 'cancel callback was called');
# TEST
is($feedback, <<'...', 'dump feedback');
* Dumped revision 0.
* Dumped revision 1.
* Dumped revision 2.
* Dumped revision 3.
...

END {
diag "cleanup";
rmtree($repospath);
}

package something;

sub DESTROY {
    $main::something_destroyed++;
}

1;
