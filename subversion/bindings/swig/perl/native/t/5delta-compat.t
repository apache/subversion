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
use Test::More tests => 2;
require SVN::Core;
require SVN::Delta;

my ($srctext, $tgttext, $result) = ('abcd===eflfjgjkx', 'abcd=--ef==lfjffgjx', '');

open my $source, '<', \$srctext;
open my $target, '<', \$tgttext;
open my $aresult, '>', \$result;

my $txstream = SVN::TxDelta::new($source, $target);

# TEST
isa_ok($txstream, '_p_svn_txdelta_stream_t');
open my $asource, '<', \$srctext;
my $handle = [SVN::TxDelta::apply($asource, $aresult, undef, undef)];

SVN::TxDelta::send_txstream($txstream, @$handle);

# TEST
is($result, $tgttext, 'delta self test');

close $aresult;
