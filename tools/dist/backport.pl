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

use Digest ();
use Term::ReadKey qw/ReadMode ReadKey/;
use File::Basename qw/basename/;
use File::Copy qw/copy move/;
use File::Temp qw/tempfile/;
use POSIX qw/ctermid/;

############### Start of reading values from environment ###############

# Programs we use.
my $SVNAUTH = $ENV{SVNAUTH} // 'svnauth'; # optional dependency
my $SVN = $ENV{SVN} || 'svn'; # passed unquoted to sh
my $SHELL = $ENV{SHELL} // '/bin/sh';
my $VIM = 'vim';
my $EDITOR = $ENV{SVN_EDITOR} // $ENV{VISUAL} // $ENV{EDITOR} // 'ed';
my $PAGER = $ENV{PAGER} // 'less -F' // 'cat';

# Mode flags.
#    svn-role:      YES=1 MAY_COMMIT=1
#    conflicts-bot: YES=1 MAY_COMMIT=0
#    interactive:   YES=0 MAY_COMMIT=0      (default)
my $YES = exists $ENV{YES}; # batch mode: eliminate prompts, add sleeps
my $MAY_COMMIT = 'false';
$MAY_COMMIT = 'true' if ($ENV{MAY_COMMIT} // "false") =~ /^(1|yes|true)$/i;

# Other knobs.
my $VERBOSE = 0;
my $DEBUG = (exists $ENV{DEBUG}) ? 'true' : 'false'; # 'set -x', etc

# Username for entering votes.
my $SVN_A_O_REALM = '<https://svn.apache.org:443> ASF Committers';            
my ($AVAILID) = $ENV{AVAILID} // do {
  local $_ = `$SVNAUTH list 2>/dev/null`;
  ($? == 0) ? (/Auth.*realm: \Q$SVN_A_O_REALM\E\nUsername: (.*)/, $1) : undef
} // do {
  my $filename = Digest->new("MD5")->add($SVN_A_O_REALM)->hexdigest;
  open USERNAME, '<', "$ENV{HOME}/.subversion/auth/svn.simple/$filename";
  1 until <USERNAME> eq "username\n";
  <USERNAME>;
  local $_ = <USERNAME>;
  chomp;
  $_
}
// warn "Username for commits (of votes/merges) not found";

############## End of reading values from the environment ##############

# Constants.
my $STATUS = './STATUS';
my $STATEFILE = './.backports1';
my $BRANCHES = '^/subversion/branches';

# Globals.
my %ERRORS = ();
my $MERGED_SOMETHING = 0;
my $SVNq;

# Derived values.
my $SVNvsn = do {
  my ($major, $minor, $patch) = `$SVN --version -q` =~ /^(\d+)\.(\d+)\.(\d+)/;
  1e6*$major + 1e3*$minor + $patch;
};
$SVN .= " --non-interactive" if $YES or not defined ctermid;
$SVNq = "$SVN -q ";
$SVNq =~ s/-q// if $DEBUG eq 'true';


sub usage {
  my $basename = basename $0;
  print <<EOF;
backport.pl: a tool for reviewing and merging STATUS entries.  Run this with
CWD being the root of the stable branch (e.g., 1.8.x).  The ./STATUS file
should be at HEAD.

In interactive mode (the default), you will be prompted once per STATUS entry.
At a prompt, you have the following options:

y:   Run a merge.  It will not be committed.
     WARNING: This will run 'update' and 'revert -R ./'.
l:   Show logs for the entries being nominated.
q:   Quit the "for each nomination" loop.
±1:  Enter a +1 or -1 vote
     You will be prompted to commit your vote at the end.
±0:  Enter a +0 or -0 vote
     You will be prompted to commit your vote at the end.
a:   Move the entry to the "Approved changes" section.
     When both approving and voting on an entry, approve first: for example,
     to enter a third +1 vote, type "a" "+" "1".
e:   Edit the entry in $EDITOR.
     You will be prompted to commit your edits at the end.
N:   Move to the next entry.  Cache the entry in '$STATEFILE' and do not
     prompt for it again (even across runs) until it is changed.
 :   Move to the next entry, without adding the current one to the cache.
     (That's a space character, ASCII 0x20.)

After running a merge, you have the following options:

y:   Open a shell.
d:   View a diff.
N:   Move to the next entry.

There is also a batch mode: when \$YES and \$MAY_COMMIT are defined to '1' i
the environment, this script will iterate the "Approved:" section, and merge
and commit each entry therein.  If only \$YES is defined, the script will
merge every nomination (including unapproved and vetoed ones), and complain
to stderr if it notices any conflicts.  These modes are normally used by the
'svn-role' cron job and/or buildbot, not by human users.

The 'svn' binary defined by the environment variable \$SVN, or otherwise the
'svn' found in \$PATH, will be used to manage the working copy.
EOF
}

sub digest_string {
  Digest->new("MD5")->add(@_)->hexdigest
}

sub prompt {
  print $_[0]; shift;
  my %args = @_;
  my $getchar = sub {
    ReadMode 'cbreak';
    my $answer = (ReadKey 0);
    ReadMode 'normal';
    print $answer;
    return $answer;
  };

  die "$0: called prompt() in non-interactive mode!" if $YES;
  my $answer = $getchar->();
  $answer .= $getchar->() if exists $args{extra} and $answer =~ $args{extra};
  say "" unless $args{dontprint};
  return $args{verbose}
         ? $answer
         : ($answer =~ /^y/i) ? 1 : 0;
}


sub merge {
  my %entry = @_;
  $MERGED_SOMETHING++;

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
elif test -z "\$YES"; then
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
elif test -z "\$YES"; then
  echo "Would remove $reintegrated_word '$entry{branch}' branch"
fi
EOF

  open SHELL, '|-', qw#/bin/sh# or die "$! (in '$entry{header}')";
  print SHELL $script;
  close SHELL or warn "$0: sh($?): $! (in '$entry{header}')";
  $ERRORS{$entry{id}} = [\%entry, "sh($?): $!"] if $?;

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

sub logsummarysummary {
  my $entry = shift;
  join "",
    $entry->{logsummary}->[0], ('[...]' x (0 < $#{$entry->{logsummary}}))
}

# TODO: may need to parse other headers too?
sub parse_entry {
  my $raw = shift;
  my @lines = @_;
  my $depends;
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

  # depends
  # TODO: parse the value of this.
  $depends = grep /^Depends:/, @_;

  # branch
  while (@_) {
    shift and next unless $_[0] =~ s/^\s*Branch(es)?:\s*//;
    $branch = sanitize_branch (shift || shift || die "Branch header found without value");
  }

  # Compute a header.
  my ($header, $id);
  $header = "r$revisions[0] group" if @revisions;
  $id = "r$revisions[0]"           if @revisions;
  $header = "$branch branch"       if $branch;
  $id = $branch                    if $branch;
  warn "No header for [@lines]" unless $header;

  return (
    revisions => [@revisions],
    logsummary => [@logsummary],
    branch => $branch,
    header => $header,
    depends => $depends,
    id => $id,
    votes => [@votes],
    entry => [@lines],
    raw => $raw,
    digest => digest_string($raw),
  );
}

sub edit_string {
  # Edits $_[0] in an editor.
  # $_[1] is used in error messages.
  die "$0: called edit_string() in non-interactive mode!" if $YES;
  my $string = shift;
  my $name = shift;
  my %args = @_;
  my $trailing_eol = $args{trailing_eol};
  my ($fh, $fn) = tempfile;
  print $fh $string;
  $fh->flush or die $!;
  system("$EDITOR -- $fn") == 0
    or warn "\$EDITOR failed editing $name: $! ($?); "
           ."edit results ($fn) ignored.";
  my $rv = `cat $fn`;
  $rv =~ s/\n*\z// and $rv .= ("\n" x $trailing_eol) if defined $trailing_eol;
  $rv;
}

sub vote {
  my ($state, $approved, $votes) = @_;
  # TODO: use votesarray instead of votescheck
  my (%approvedcheck, %votescheck);
  my $raw_approved = "";
  my @votesarray;
  return unless %$approved or %$votes;

  my $had_empty_line;

  $. = 0;
  open STATUS, "<", $STATUS;
  open VOTES, ">", "$STATUS.$$.tmp";
  while (<STATUS>) {
    $had_empty_line = /\n\n\z/;
    my $key = digest_string $_;

    $approvedcheck{$key}++ if exists $approved->{$key};
    $votescheck{$key}++ if exists $votes->{$key};

    unless (exists $votes->{$key} or exists $approved->{$key}) {
      print VOTES;
      next;
    }

    unless (exists $votes->{$key}) {
      push @votesarray, {
        entry => $approved->{$key},
        approval => 1,
        digest => $key,
      };
      $raw_approved .= $_;
      next;
    }

    # We have a vote, and potentially an approval.

    my ($vote, $entry) = @{$votes->{$key}};
    push @votesarray, {
      entry => $entry,
      vote => $vote,
      approval => (exists $approved->{$key}),
      digest => $key,
    };

    if ($vote eq 'edit') {
      local $_ = $entry->{raw};
      $votesarray[-1]->{digest} = digest_string $_;
      (exists $approved->{$key}) ? ($raw_approved .= $_) : (print VOTES);
      next;
    }
    
    s/^(\s*\Q$vote\E:.*)/"$1, $AVAILID"/me
    or s/(.*\w.*?\n)/"$1     $vote: $AVAILID\n"/se;
    $_ = edit_string $_, $entry->{header}, trailing_eol => 2
        if $vote ne '+1';
    $votesarray[-1]->{digest} = digest_string $_;
    (exists $approved->{$key}) ? ($raw_approved .= $_) : (print VOTES);
  }
  close STATUS;
  print VOTES "\n" if $raw_approved and !$had_empty_line;
  print VOTES $raw_approved;
  close VOTES;
  warn "Some vote chunks weren't found: ",
    join ',',
    map $votes->{$_}->[1]->{id},
    grep { !$votescheck{$_} } keys %$votes
    if scalar(keys %$votes) != scalar(keys %votescheck);
  warn "Some approval chunks weren't found: ",
    join ',',
    map $approved->{$_}->{id},
    grep { !$approvedcheck{$_} } keys %$approved
    if scalar(keys %$approved) != scalar(keys %approvedcheck);
  prompt "Press the 'any' key to continue...\n", dontprint => 1
    if scalar(keys %$approved) != scalar(keys %approvedcheck) 
    or scalar(keys %$votes) != scalar(keys %votescheck);
  move "$STATUS.$$.tmp", $STATUS;

  my $logmsg = do {
    my @sentences = map {
       my $words_vote = ", approving" x $_->{approval};
       my $words_edit = " and approve" x $_->{approval};
       exists $_->{vote}
       ? (
         ( $_->{vote} eq 'edit'
           ? "Edit$words_edit the $_->{entry}->{id} entry"
           : "Vote $_->{vote} on the $_->{entry}->{header}$words_vote"
         )
         . "."
         )
      : # exists only in $approved
        "Approve the $_->{entry}->{header}."
      } @votesarray;
    (@sentences == 1)
    ? $sentences[0]
    : "* STATUS:\n" . join "", map "  $_\n", @sentences;
  };

  system "$SVN diff -- $STATUS";
  printf "[[[\n%s%s]]]\n", $logmsg, ("\n" x ($logmsg !~ /\n\z/));
  if (prompt "Commit these votes? ") {
    my ($logmsg_fh, $logmsg_filename) = tempfile();
    print $logmsg_fh $logmsg;
    close $logmsg_fh;
    warn("Tempfile name '$logmsg_filename' not shell-safe; "
         ."refraining from commit.\n") and return
        unless $logmsg_filename =~ /^([A-Z0-9._-]|\x2f)+$/i;
    system("$SVN commit -F $logmsg_filename -- $STATUS") == 0
        or warn("Committing the votes failed($?): $!") and return;
    unlink $logmsg_filename;

    $state->{$_->{digest}}++ for @votesarray;
  }
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
  revert if !$YES and $MERGED_SOMETHING and prompt 'Revert? ';
  (@_ ? exit : return);
}

sub warning_summary {
  return unless %ERRORS;

  warn "Warning summary\n";
  warn "===============\n";
  warn "\n";
  for my $header (keys %ERRORS) {
    my $title = logsummarysummary $ERRORS{$header}->[0];
    warn "$header ($title): $ERRORS{$header}->[1]\n";
  }
}

sub read_state {
  # die "$0: called read_state() in non-interactive mode!" if $YES;

  open my $fh, '<', $STATEFILE or do {
    return {} if $!{ENOENT};
    die "Can't read statefile: $!";
  };

  my %rv;
  while (<$fh>) {
    chomp;
    $rv{$_}++;
  }
  return \%rv;
}

sub write_state {
  my $state = shift;
  open STATE, '>', $STATEFILE or warn("Can't write state: $!"), return;
  say STATE for keys %$state;
  close STATE;
}

sub exit_stage_left {
  my $state = shift;
  maybe_revert;
  warning_summary if $YES;
  vote $state, @_;
  write_state $state;
  exit scalar keys %ERRORS;
}

sub handle_entry {
  my $in_approved = shift;
  my $approved = shift;
  my $votes = shift;
  my $state = shift;
  my $raw = shift;
  my %entry = parse_entry $raw, @_;
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

        my $output = `$SVN status`;
        my (@conflicts) = ($output =~ m#^(?:C...|.C..|...C)...\s(.*)#mg);
        if (@conflicts and !$entry{depends}) {
          $ERRORS{$entry{id}} //= [\%entry,
                                   sprintf "Conflicts on %s%s%s",
                                     '[' x !!$#conflicts,
                                     (join ', ',
                                      map { basename $_ }
                                      @conflicts),
                                     ']' x !!$#conflicts,
                                  ];
          say STDERR "Conflicts merging the $entry{header}!";
          say STDERR "";
          say STDERR $output;
          system "$SVN diff -- @conflicts";
        } elsif (!@conflicts and $entry{depends}) {
          # Not a warning since svn-role may commit the dependency without
          # also committing the dependent in the same pass.
          print "No conflicts merging $entry{id}, but conflicts were "
              ."expected ('Depends:' header set)\n";
        } elsif (@conflicts) {
          say "Conflicts found merging $entry{id}, as expected.";
        }
        revert;
      }
    }
  } elsif ($state->{$entry{digest}}) {
    print "\n\n";
    say "Skipping the $entry{header} (remove $STATEFILE to reset):";
    say logsummarysummary \%entry;
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

    # See above for why the while(1).
    QUESTION: while (1) {
    my $key = $entry{digest};
    given (prompt 'Run a merge? [y,l,±1,±0,q,e,a, ,N] ',
                   verbose => 1, extra => qr/[+-]/) {
      when (/^y/i) {
        merge %entry;
        while (1) { 
          given (prompt "Shall I open a subshell? [ydN] ", verbose => 1) {
            when (/^y/i) {
              system($SHELL) == 0
                or warn "Creating an interactive subshell failed ($?): $!"
            }
            when (/^d/) {
              system("$SVN diff | $PAGER") == 0
                or warn "diff failed ($?): $!";
              next;
            }
          }
          revert;
          next PROMPT;
        }
        # NOTREACHED
      }
      when (/^l/i) {
        if ($entry{branch}) {
            system "$SVN log --stop-on-copy -v -r 0:HEAD -- "
                   ."$BRANCHES/$entry{branch} "
                   ."| $PAGER";
        } elsif (@{$entry{revisions}}) {
            system "$SVN log ".(join ' ', map { "-r$_" } @{$entry{revisions}})
                   ." -- ^/subversion | $PAGER";
        } else {
            die "Assertion failed: entry has neither branch nor revisions:\n",
                '[[[', (join ';;', %entry), ']]]';
        }
        next PROMPT;
      }
      when (/^q/i) {
        exit_stage_left $state, $approved, $votes;
      }
      when (/^a/i) {
        $approved->{$key} = \%entry;
        next PROMPT;
      }
      when (/^([+-][01])\s*$/i) {
        $votes->{$key} = [$1, \%entry];
        say "Your '$1' vote has been recorded." if $VERBOSE;
        last PROMPT;
      }
      when (/^e/i) {
        my $original = $entry{raw};
        $entry{raw} = edit_string $entry{raw}, $entry{header},
                        trailing_eol => 2;
        $votes->{$key} = ['edit', \%entry] # marker for the 2nd pass
            if $original ne $entry{raw};
        last PROMPT;
      }
      when (/^N/i) {
        $state->{$entry{digest}}++;
        last PROMPT;
      }
      when (/^\x20/) {
        last PROMPT; # Fall off the end of the given/when block.
      }
      default {
        say "Please use one of the options in brackets (q to quit)!";
        next QUESTION;
      }
    }
    last; } # QUESTION
    last; } # PROMPT
  }

  # TODO: merge() changes ./STATUS, which we're reading below, but
  #       on my system the loop in main() doesn't seem to care.

  1;
}


sub main {
  my %approved;
  my %votes;
  my $state = read_state;

  usage, exit 0 if @ARGV;

  open STATUS, "<", $STATUS or (usage, exit 1);

  # Because we use the ':normal' command in Vim...
  die "A vim with the +ex_extra feature is required for \$MAY_COMMIT mode"
      if $MAY_COMMIT eq 'true' and `${VIM} --version` !~ /[+]ex_extra/;

  # ### TODO: need to run 'revert' here
  # ### TODO: both here and in merge(), unlink files that previous merges added
  # When running from cron, there shouldn't be local mods.  (For interactive
  # usage, we preserve local mods to STATUS.)
  system("$SVN info $STATUS >/dev/null") == 0
    or die "$0: svn error; point \$SVN to an appropriate binary";

  if (`$SVN status -q $STATUS`) {
    die  "Local mods to STATUS file $STATUS" if $YES;
    warn "Local mods to STATUS file $STATUS";
    system "$SVN diff -- $STATUS";
    prompt "Press the 'any' key to continue...\n", dontprint => 1;
  }

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
        handle_entry $in_approved, \%approved, \%votes, $state, $lines, @lines;
      }
      default {
        warn "Unknown entry '$lines[0]'";
      }
    }
  }

  exit_stage_left $state, \%approved, \%votes;
}

&main
