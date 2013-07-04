#!/usr/bin/perl
use warnings;
use strict;
use feature qw/switch say/;

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

use Term::ReadKey qw/ReadMode ReadKey/;
use File::Copy qw/copy move/;
use File::Temp qw/tempfile/;
use POSIX qw/ctermid/;

my $SVN = $ENV{SVN} || 'svn'; # passed unquoted to sh
my $VIM = 'vim';
my $STATUS = './STATUS';
my $BRANCHES = '^/subversion/branches';

my $YES = !!$ENV{YES}; # batch mode: eliminate prompts, add sleeps
my $MAY_COMMIT = qw[false true][0];
my $DEBUG = qw[false true][0]; # 'set -x', etc
my ($AVAILID) = $ENV{AVAILID} // do {
  my $SVN_A_O_REALM = 'd3c8a345b14f6a1b42251aef8027ab57';
  open USERNAME, '<', "$ENV{HOME}/.subversion/auth/svn.simple/$SVN_A_O_REALM";
  1 until <USERNAME> eq "username\n";
  <USERNAME>;
  local $_ = <USERNAME>;
  chomp;
  $_
}
// warn "Username for commits (of votes/merges) not found";
my $EDITOR = $ENV{SVN_EDITOR} // $ENV{VISUAL} // $ENV{EDITOR} // 'ed';
my $VERBOSE = 0;
$DEBUG = 'true' if exists $ENV{DEBUG};
$MAY_COMMIT = 'true' if ($ENV{MAY_COMMIT} // "false") =~ /^(1|yes|true)$/i;

# derived values
my $SVNq;
my $SVNvsn = do {
  my ($major, $minor, $patch) = `$SVN --version -q` =~ /^(\d+)\.(\d+)\.(\d+)/;
  1e6*$major + 1e3*$minor + $patch;
};

$SVN .= " --non-interactive" if $YES or not defined ctermid;
$SVNq = "$SVN -q ";
$SVNq =~ s/-q// if $DEBUG eq 'true';

sub usage {
  my $basename = $0;
  $basename =~ s#.*/##;
  print <<EOF;
backport.pl: a tool for reviewing and merging STATUS entries.  Run this with
CWD being the root of the stable branch (e.g., 1.8.x).  The ./STATUS file
should be at HEAD.

In interactive mode (the default), you will be prompted once per STATUS entry.
At a prompt, you have the following options:

y:   Run a merge.  It will not be committed.
     WARNING: This will run 'update' and 'revert -R ./'.
q:   Quit the "for each nomination" loop.
±1:  Enter a +1 or -1 vote
     You will be prompted to commit your vote at the end.
±0:  Enter a +0 or -0 vote
     You will be prompted to commit your vote at the end.
e:   Edit the entry in $EDITOR.
     You will be prompted to commit your edits at the end.
N:   Move to the next entry.

After running a merge, you have the following options:

y:   Open a shell.
d:   View a diff.
N:   Move to the next entry.

There is also a batch mode: when \$YES and \$MAY_COMMIT are defined to '1' i
the environment, this script will iterate the "Approved:" section, and merge
and commit each entry therein.  If only \$YES is defined, the script will
merge every nomination (including unapproved and vetoed ones), and complain
to stderr if it notices any conflicts.  These mode are normally used by the
'svn-role' cron job and/or buildbot, not by human users.

The 'svn' binary defined by the environment variable \$SVN, or otherwise the
'svn' found in \$PATH, will be used to manage the working copy.
EOF
}

sub prompt {
  print "$_[0] "; shift;
  my %args = @_;
  my $getchar = sub {
    ReadMode 'cbreak';
    my $answer = (ReadKey 0);
    ReadMode 'cbreak';
    print $answer;
    return $answer;
  };

  die "$0: called prompt() in non-interactive mode!" if $YES;
  my $answer = $getchar->();
  $answer .= $getchar->() if exists $args{extra} and $answer =~ $args{extra};
  say "";
  return $args{verbose}
         ? $answer
         : ($answer =~ /^y/i) ? 1 : 0;
}

sub merge {
  my %entry = @_;

  my ($logmsg_fh, $logmsg_filename) = tempfile();
  my ($mergeargs, $pattern);

  my $backupfile = "backport_pl.$$.tmp";

  if ($entry{branch}) {
    # NOTE: This doesn't escape the branch into the pattern.
    $pattern = sprintf '\V\(%s branch(es)?\|branches\/%s\|Branch\(es\)\?: \*\n\? \*%s\)', $entry{branch}, $entry{branch}, $entry{branch};
    if ($SVNvsn >= 1_008_000) {
      $mergeargs = "$BRANCHES/$entry{branch}";
      say $logmsg_fh "Merge the $entry{header}:";
    } else {
      $mergeargs = "--reintegrate $BRANCHES/$entry{branch}";
      say $logmsg_fh "Reintegrate the $entry{header}:";
    }
    say $logmsg_fh "";
  } elsif (@{$entry{revisions}}) {
    $pattern = '^ [*] \V' . 'r' . $entry{revisions}->[0];
    $mergeargs = join " ", (map { "-c$_" } @{$entry{revisions}}), '^/subversion/trunk';
    if (@{$entry{revisions}} > 1) {
      say $logmsg_fh "Merge the $entry{header} from trunk:";
      say $logmsg_fh "";
    } else {
      say $logmsg_fh "Merge r$entry{revisions}->[0] from trunk:";
      say $logmsg_fh "";
    }
  } else {
    die "Don't know how to call $entry{header}";
  }
  say $logmsg_fh $_ for @{$entry{entry}};
  close $logmsg_fh or die "Can't close $logmsg_filename: $!";

  my $reintegrated_word = ($SVNvsn >= 1_008_000) ? "merged" : "reintegrated";
  my $script = <<"EOF";
#!/bin/sh
set -e
if $DEBUG; then
  set -x
fi
$SVN diff > $backupfile
if ! $MAY_COMMIT ; then
  cp STATUS STATUS.$$
fi
$SVNq revert -R .
if ! $MAY_COMMIT ; then
  mv STATUS.$$ STATUS
fi
$SVNq up
$SVNq merge $mergeargs
if [ "`$SVN status -q | wc -l`" -eq 1 ]; then
  if [ -n "`$SVN diff | perl -lne 'print if s/^(Added|Deleted|Modified): //' | grep -vx svn:mergeinfo`" ]; then
    # This check detects STATUS entries that name non-^/subversion/ revnums.
    # ### Q: What if we actually commit a mergeinfo fix to trunk and then want
    # ###    to backport it?
    # ### A: We don't merge it using the script.
    echo "Bogus merge: includes only svn:mergeinfo changes!" >&2
    exit 2
  fi
fi
if $MAY_COMMIT; then
  $VIM -e -s -n -N -i NONE -u NONE -c '/$pattern/normal! dap' -c wq $STATUS
  $SVNq commit -F $logmsg_filename
elif test 1 -ne $YES; then
  echo "Would have committed:"
  echo '[[['
  $SVN status -q
  echo 'M       STATUS (not shown in the diff)'
  cat $logmsg_filename
  echo ']]]'
fi
EOF

  $script .= <<"EOF" if $entry{branch};
reinteg_rev=\`$SVN info $STATUS | sed -ne 's/Last Changed Rev: //p'\`
if $MAY_COMMIT; then
  # Sleep to avoid out-of-order commit notifications
  if [ -n "\$YES" ]; then sleep 15; fi
  $SVNq rm $BRANCHES/$entry{branch} -m "Remove the '$entry{branch}' branch, $reintegrated_word in r\$reinteg_rev."
  if [ -n "\$YES" ]; then sleep 1; fi
elif test 1 -ne $YES; then
  echo "Would remove $reintegrated_word '$entry{branch}' branch"
fi
EOF

  open SHELL, '|-', qw#/bin/sh# or die "$! (in '$entry{header}')";
  print SHELL $script;
  close SHELL or warn "$0: sh($?): $! (in '$entry{header}')";

  if (-z $backupfile) {
    unlink $backupfile;
  } else {
    warn "Local mods saved to '$backupfile'\n";
  }

  unlink $logmsg_filename unless $? or $!;
}

sub sanitize_branch {
  local $_ = shift;
  s#.*/##;
  s/^\s*//;
  s/\s*$//;
  return $_;
}

# TODO: may need to parse other headers too?
sub parse_entry {
  my $lines = shift;
  my @lines = @_;
  my (@revisions, @logsummary, $branch, @votes);
  # @lines = @_;

  # strip first three spaces
  $_[0] =~ s/^ \* /   /;
  s/^   // for @_;

  # revisions
  $branch = sanitize_branch $1
    if $_[0] =~ /^(\S*) branch$/ or $_[0] =~ m#branches/(\S+)#;
  while ($_[0] =~ /^r/) {
    my $sawrevnum = 0;
    while ($_[0] =~ s/^r(\d+)(?:$|[,; ]+)//) {
      push @revisions, $1;
      $sawrevnum++;
    }
    $sawrevnum ? shift : last;
  }

  # summary
  do {
    push @logsummary, shift
  } until $_[0] =~ /^\s*\w+:/ or not defined $_[0];

  # votes
  unshift @votes, pop until $_[-1] =~ /^\s*Votes:/ or not defined $_[-1];
  pop;

  # branch
  while (@_) {
    shift and next unless $_[0] =~ s/^\s*Branch(es)?:\s*//;
    $branch = sanitize_branch (shift || shift || die "Branch header found without value");
  }

  # Compute a header.
  my $header;
  $header = "r$revisions[0] group" if @revisions;
  $header = "$branch branch" if $branch;
  warn "No header for [@lines]" unless $header;

  return (
    revisions => [@revisions],
    logsummary => [@logsummary],
    branch => $branch,
    header => $header,
    votes => [@votes],
    entry => [@lines],
    raw => $lines,
  );
}

sub edit_string {
  # Edits $_[0] in an editor.
  # $_[1] is used in error messages.
  die "$0: called edit_string() in non-interactive mode!" if $YES;
  my $string = shift;
  my $name = shift;
  my ($fh, $fn) = tempfile;
  print $fh $string;
  $fh->flush or die $!;
  system("$EDITOR -- $fn") == 0
    or warn "\$EDITOR failed editing $name: $! ($?); "
           ."edit results ($fn) ignored.";
  return `cat $fn`;
}

sub vote {
  my $votes = shift;
  return unless %$votes;

  $. = 0;
  open STATUS, "<", $STATUS;
  open VOTES, ">", "$STATUS.$$.tmp";
  while (<STATUS>) {
    unless (exists $votes->{$.}) {
      print VOTES;
      next;
    }

    my ($vote, $entry) = @{delete $votes->{$.}};
    say "Voting '$vote' on $entry->{header}";

    if ($vote eq 'edit') {
      print VOTES $entry->{raw};
      next;
    }
    
    s/^(\s*\Q$vote\E:.*)/"$1, $AVAILID"/me
    or s/(.*\w.*?\n)/"$1     $vote: $AVAILID\n"/se;
    print VOTES;
  }
  close STATUS;
  close VOTES;
  die "Some vote chunks weren't found: ", keys %$votes if %$votes;
  move "$STATUS.$$.tmp", $STATUS;

  system $SVN, qw/diff --/, $STATUS;
  system $SVN, qw/commit -m Vote. --/, $STATUS
    if prompt "Commit these votes? ";
}

sub revert {
  copy $STATUS, "$STATUS.$$.tmp";
  system "$SVN revert -q $STATUS";
  system "$SVN revert -R ./" . ($YES && $MAY_COMMIT ne 'true'
                             ? " -q" : "");
  move "$STATUS.$$.tmp", $STATUS;
}

sub maybe_revert {
  # This is both a SIGINT handler, and the tail end of main() in normal runs.
  # @_ is 'INT' in the former case and () in the latter.
  delete $SIG{INT} unless @_;
  return if $YES or not prompt 'Revert? ';
  revert;
  exit if @_;
}

sub exit_stage_left {
  maybe_revert;
  vote shift;
  exit;
}

sub handle_entry {
  my $in_approved = shift;
  my $votes = shift;
  my $lines = shift;
  my %entry = parse_entry $lines, @_;
  my @vetoes = grep { /^  -1:/ } @{$entry{votes}};

  if ($YES) {
    # Run a merge if:
    unless (@vetoes) {
      if ($MAY_COMMIT eq 'true' and $in_approved) {
        # svn-role mode
        merge %entry;
      } elsif ($MAY_COMMIT ne 'true') {
        # Scan-for-conflicts mode
        merge %entry;

        my $output;
        if (($output = `$SVN status`) =~ /^(C|.C|...C)/m) {
          say STDERR "Conflicts merging the $entry{header}!";
          say STDERR "";
          say STDERR $output;
        }
        revert;
      }
    }
  } else {
    # This loop is just a hack because 'goto' panics.  The goto should be where
    # the "next PROMPT;" is; there's a "last;" at the end of the loop body.
    PROMPT: while (1) {
    say "";
    say "\n>>> The $entry{header}:";
    say join ", ", map { "r$_" } @{$entry{revisions}} if @{$entry{revisions}};
    say "$BRANCHES/$entry{branch}" if $entry{branch};
    say "";
    say for @{$entry{logsummary}};
    say "";
    say for @{$entry{votes}};
    say "";
    say "Vetoes found!" if @vetoes;

    given (prompt 'Go ahead? [y,±1,±0,q,e,N]',
                   verbose => 1, extra => qr/[+-]/) {
      when (/^y/i) {
        merge %entry;
        while (1) { 
          given (prompt "Shall I open a subshell? [ydN]", verbose => 1) {
            when (/^y/i) {
              system($ENV{SHELL} // "/bin/sh") == 0
                or warn "Creating an interactive subshell failed ($?): $!"
            }
            when (/^d/) {
              system($SVN, 'diff') == 0
                or warn "diff failed ($?): $!";
              next;
            }
          }
          revert;
          next PROMPT;
        }
        # NOTREACHED
      }
      when (/^q/i) {
        exit_stage_left $votes;
      }
      when (/^([+-][01])\s*$/i) {
        $votes->{$.} = [$1, \%entry];
        say "Your '$1' vote has been recorded." if $VERBOSE;
      }
      when (/^e/i) {
        $entry{raw} = edit_string $entry{raw}, $entry{header};
        $votes->{$.} = ['edit', \%entry]; # marker for the 2nd pass
      }
    }
    last;
    }
  }

  # TODO: merge() changes ./STATUS, which we're reading below, but
  #       on my system the loop in main() doesn't seem to care.

  1;
}

sub main {
  my %votes;

  usage, exit 0 if @ARGV;

  open STATUS, "<", $STATUS or (usage, exit 1);

  # Because we use the ':normal' command in Vim...
  die "A vim with the +ex_extra feature is required"
      if `${VIM} --version` !~ /[+]ex_extra/;

  # ### TODO: need to run 'revert' here
  # ### TODO: both here and in merge(), unlink files that previous merges added
  # When running from cron, there shouldn't be local mods.  (For interactive
  # usage, we preserve local mods to STATUS.)
  die "Local mods to STATUS file $STATUS" if $YES and `$SVN status -q $STATUS`;

  # Skip most of the file
  $/ = ""; # paragraph mode
  while (<STATUS>) {
    last if /^Status of \d+\.\d+/;
  }

  $SIG{INT} = \&maybe_revert unless $YES;

  my $in_approved = 0;
  while (<STATUS>) {
    my $lines = $_;
    my @lines = split /\n/;

    given ($lines[0]) {
      # Section header
      when (/^[A-Z].*:$/i) {
        say "\n\n=== $lines[0]" unless $YES;
        $in_approved = $lines[0] =~ /^Approved changes/;
      }
      # Comment
      when (/^[#\x5b]/i) {
        next;
      }
      # Separator after section header
      when (/^=+$/i) {
        break;
      }
      # Backport entry?
      when (/^ \*/) {
        warn "Too many bullets in $lines[0]" and next
          if grep /^ \*/, @lines[1..$#lines];
        handle_entry $in_approved, \%votes, $lines, @lines;
      }
      default {
        warn "Unknown entry '$lines[0]' at line $.\n";
      }
    }
  }

  exit_stage_left \%votes;
}

&main
