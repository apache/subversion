#!/usr/bin/perl -w

use Test::More tests => 7;
use strict;
BEGIN {
require_ok 'SVN::Core';
require_ok 'SVN::Repos';
require_ok 'SVN::Fs';
require_ok 'SVN::Delta';
require_ok 'SVN::Ra';
require_ok 'SVN::Wc';
require_ok 'SVN::Client';
}
