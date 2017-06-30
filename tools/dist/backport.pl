#!/usr/bin/perl
use warnings;
use strict;
use feature qw/switch say/;

use v5.10.0; # needed for $^V

# The given/when smartmatch facility, introduced in Perl v5.10, was made
# experimental and "subject to change" in v5.18 (see perl5180delta).  Every
# use of it now triggers a warning.
#
# As of Perl v5.24.1, the semantics of given/when provided by Perl are
# compatible with those expected by the script, so disable the warning for
# those Perls.  But don't try to disable the the warning category on Perls
# that don't know that category, since that breaks compilation.
no if (v5.17.0 le $^V and $^V le v5.24.1),
   warnings => 'experimental::smartmatch';

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

use Carp qw/croak confess carp cluck/;
use Digest ();
use Term::ReadKey qw/ReadMode ReadKey/;
use File::Basename qw/basename dirname/;
use File::Copy qw/copy move/;
use File::Temp qw/tempfile/;
use IO::Select ();
use IPC::Open3 qw/open3/;
use POSIX qw/ctermid strftime/;
use Text::Wrap qw/wrap/;
use Tie::File ();

############### Start of reading values from environment ###############

# Programs we use.
#
# TODO: document which are interpreted by sh and which should point to binary.
my $SVN = $ENV{SVN} || 'svn'; # passed unquoted to sh
$SVN .= " --config-option=config:miscellany:log-encoding=UTF-8";
my $SHELL = $ENV{SHELL} // '/bin/sh';
my $VIM = 'vim';
my $EDITOR = $ENV{SVN_EDITOR} // $ENV{VISUAL} // $ENV{EDITOR} // 'ed';
my $PAGER = $ENV{PAGER} // 'less' // 'cat';

# Mode flags.
package Mode {
  use constant {
    AutoCommitApproveds => 1, # used by nightly commits (svn-role)
    Conflicts => 2,           # used by the hourly conflicts-detection buildbot
    Interactive => 3,
  };
};
my $YES = ($ENV{YES} // "0") =~ /^(1|yes|true)$/i; # batch mode: eliminate prompts, add sleeps
my $MAY_COMMIT = ($ENV{MAY_COMMIT} // "false") =~ /^(1|yes|true)$/i;
my $MODE = ($YES ? ($MAY_COMMIT ? Mode::AutoCommitApproveds : Mode::Conflicts )
                 : Mode::Interactive );

# Other knobs.
my $VERBOSE = 0;
my $DEBUG = (exists $ENV{DEBUG}); # 'set -x', etc

# Force all these knobs to be usable via @sh.
my @sh = qw/false true/;
die if grep { ($sh[$_] eq 'true') != !!$_ } $DEBUG, $MAY_COMMIT, $VERBOSE, $YES;

# Username for entering votes.
my $SVN_A_O_REALM = '<https://svn.apache.org:443> ASF Committers';
my ($AVAILID) = $ENV{AVAILID} // do {
  local $_ = `$SVN auth svn.apache.org:443 2>/dev/null`; # TODO: pass $SVN_A_O_REALM
  ($? == 0 && /Auth.*realm: \Q$SVN_A_O_REALM\E\nUsername: (.*)/) ? $1 : undef
} // do {
  local $/; # slurp mode
  my $fh;
  my $dir = "$ENV{HOME}/.subversion/auth/svn.simple/";
  my $filename = Digest->new("MD5")->add($SVN_A_O_REALM)->hexdigest;
  open $fh, '<', "$dir/$filename"
  and <$fh> =~ /K 8\nusername\nV \d+\n(.*)/
  ? $1
  : undef
};

unless (defined $AVAILID) {
  unless ($MODE == Mode::Conflicts) {
    warn "Username for commits (of votes/merges) not found; "
         ."it will be possible to review nominations but not to commit votes "
         ."or merges.\n";
    warn "Press the 'any' key to continue...\n";
    die if $MODE == Mode::AutoCommitApproveds; # unattended mode; can't prompt.
    ReadMode 'cbreak';
    ReadKey 0;
    ReadMode 'restore';
  }
}

############## End of reading values from the environment ##############

# Constants.
my $STATUS = './STATUS';
my $STATEFILE = './.backports1';
my $BRANCHES = '^/subversion/branches';
my $TRUNK = '^/subversion/trunk';
$ENV{LC_ALL} = "C";  # since we parse 'svn info' output

# Globals.
my %ERRORS = ();
# TODO: can $MERGED_SOMETHING be removed and references to it replaced by scalar(@MERGES_TODAY) ?
#       alternately, does @MERGES_TODAY need to be purged whenever $MERGED_SOMETHING is reset?
#       The scalar is only used in interactive runs, but the array is used in
#       svn-role batch mode too.
my @MERGES_TODAY;
my $MERGED_SOMETHING = 0;
my $SVNq;

# Derived values.
my $SVNvsn = do {
  my ($major, $minor, $patch) = `$SVN --version -q` =~ /^(\d+)\.(\d+)\.(\d+)/;
  1e6*$major + 1e3*$minor + $patch;
};
$SVN .= " --non-interactive" if $YES or not defined ctermid;
$SVNq = "$SVN -q ";
$SVNq =~ s/-q// if $DEBUG;


my $BACKPORT_OPTIONS_HELP = <<EOF;
y:   Run a merge.  It will not be committed.
     WARNING: This will run 'update' and 'revert -R ./'.
l:   Show logs for the entries being nominated.
v:   Show the full entry (the prompt only shows an abridged version).
q:   Quit the "for each entry" loop.  If you have entered any votes or
     approvals, you will be prompted to commit them.
±1:  Enter a +1 or -1 vote
     You will be prompted to commit your vote at the end.
±0:  Enter a +0 or -0 vote
     You will be prompted to commit your vote at the end.
a:   Move the entry to the "Approved changes" section.
     When both approving and voting on an entry, approve first: for example,
     to enter a third +1 vote, type "a" "+" "1".
e:   Edit the entry in \$EDITOR, which is '$EDITOR'.
     You will be prompted to commit your edits at the end.
N:   Move to the next entry.  Do not prompt for the current entry again, even
     in future runs, unless the STATUS nomination has been modified (e.g.,
     revisions added, justification changed) in the repository.
     (This is a local action that will not affect other people or bots.)
 :   Move to the next entry.  Prompt for the current entry again in the next
     run of backport.pl. 
     (That's a space character, ASCII 0x20.)
?:   Display this list.
EOF

my $BACKPORT_OPTIONS_MERGE_OPTIONS_HELP = <<EOF;
y:   Open a shell.
d:   View a diff.
N:   Move to the next entry.
?:   Display this list.
EOF

sub backport_usage {
  my $basename = basename $0;
  print <<EOF;
backport.pl: a tool for reviewing, merging, and voting on STATUS entries.

Normally, invoke this with CWD being the root of the stable branch (e.g.,
1.8.x):

    Usage: test -e \$d/STATUS && cd \$d && \\
           backport.pl [PATTERN]
    (where \$d is a working copy of branches/1.8.x)

Alternatively, invoke this via a symlink named "b" placed at the same directory
as the STATUS file, in which case the CWD doesn't matter (the script will cd):

    Usage: ln -s /path/to/backport.pl \$d/b && \\
           \$d/b [PATTERN]
    (where \$d is a working copy of branches/1.8.x)

In either case, the ./STATUS file should be at HEAD.  If it has local mods,
they will be preserved through 'revert' operations but included in 'commit'
operations.

If PATTERN is provided, only entries which match PATTERN are considered.  The
sense of "match" is either substring (fgrep) or Perl regexp (with /msi).

In interactive mode (the default), you will be prompted once per STATUS entry.
At a prompt, you have the following options:

$BACKPORT_OPTIONS_HELP

After running a merge, you have the following options:

$BACKPORT_OPTIONS_MERGE_OPTIONS_HELP

To commit a merge, you have two options: either answer 'y' to the second prompt
to open a shell, and manually run 'svn commit' therein; or set \$MAY_COMMIT=1
in the environment before running the script, in which case answering 'y'
to the first prompt will not only run the merge but also commit it.

There are two batch modes.  The first mode is used by the nightly svn-role
mergebot.  It is enabled by setting \$YES and \$MAY_COMMIT to '1' in the
environment.  In this mode, the script will iterate the "Approved changes:"
section and merge and commit each entry therein.  To prevent an entry from
being auto-merged, veto it or move it to a new section named "Approved, but
merge manually:".

The second batch mode is used by the hourly conflicts detector bot.  It is
triggered by having \$YES defined in the environment to '1' and \$MAY_COMMIT
undefined.  In this mode, the script will locally merge every nomination
(including unapproved and vetoed ones), and complain to stderr if the merge
failed due to a conflict.  This mode never commits anything.

The hourly conflicts detector bot turns red if any entry produced a merge
conflict.  When entry A depends on entry B for a clean merge, put a "Depends:"
header on entry A to instruct the bot not to turn red due to A.  (The header
is not parsed; only its presence or absence matters.)

Both batch modes also perform a basic sanity-check on entries that declare
backport branches (via the "Branch:" header): if a backport branch is used, but
at least one of the revisions enumerated in the entry title had neither been
merged from $TRUNK to the branch root, nor been committed
directly to the backport branch, the hourly bot will turn red and 
nightly bot will skip the entry and email its admins.  (The nightly bot does
not email the list on failure, since it doesn't use buildbot.)

The 'svn' binary defined by the environment variable \$SVN, or otherwise the
'svn' found in \$PATH, will be used to manage the working copy.
EOF
}

sub nominate_usage {
  my $availid = $AVAILID // "(your username)";
  my $basename = basename $0;
  print <<EOF;
nominate.pl: a tool for adding entries to STATUS.

Usage: $0 "r42, r43, r45" "\$Some_justification"

Will add:
 * r42, r43, r45
   (log message of r42)
   Justification:
     \$Some_justification
   Votes:
     +1: $availid
to STATUS.  Backport branches are detected automatically.

The revisions argument may contain arbitrary text (besides the revision
numbers); it will be ignored.  For example,
    $0 "Committed revision 42." "\$Some_justification"
will nominate r42.

The justification can be an arbitrarily-long string; if it is wider than the
available width, this script will wrap it for you (and allow you to review
the result before committing).

The STATUS file in the current directory is used (unless argv[0] is "n", in
which case the STATUS file in the directory of argv[0] is used; the intent
is to create a symlink named "n" in the branch wc root).

EOF
# TODO: Optionally add a "Notes" section.
# TODO: Look for backport branches named after issues.
# TODO: Do a dry-run merge on added entries.
# TODO: Do a dry-run merge on interactively-edited entries in backport.pl
}

# If $AVAILID is undefined, warn about it and return true.
# Else return false.
#
# $_[0] is a string for inclusion in generated error messages.
sub warned_cannot_commit {
  my $caller_error_string = shift;
  return 0 if defined $AVAILID;

  warn "$0: $caller_error_string: unable to determine your username via \$AVAILID or svnauth(1) or ~/.subversion/auth/";
  return 1;
}

sub digest_string {
  Digest->new("MD5")->add(@_)->hexdigest
}

sub digest_entry($) {
  # Canonicalize the number of trailing EOLs to two.  This matters when there's
  # on empty line after the last entry in Approved, for example.
  local $_ = shift;
  s/\n*\z// and $_ .= "\n\n";
  digest_string($_)
}

sub prompt {
  print $_[0]; shift;
  my %args = @_;
  my $getchar = sub {
    my $answer;
    do {
      ReadMode 'cbreak';
      $answer = (ReadKey 0);
      ReadMode 'normal';
      die if $@ or not defined $answer;
      # Swallow terminal escape codes (e.g., arrow keys).
      unless ($answer =~ m/^(?:[[:print:]]+|\s+)$/) {
        $answer = (ReadKey -1) while defined $answer;
        # TODO: provide an indication that the keystroke was sensed and ignored.
      }
    } until defined $answer and ($answer =~ m/^(?:[[:print:]]+|\s+)$/);
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

# Bourne-escape a string.
# Example:
#     >>> shell_escape(q[foo'bar]) eq q['foo'\''bar']
#     True
sub shell_escape {
  my (@reply) = map {
    local $_ = $_; # the LHS $_ is mutable; the RHS $_ may not be.
    s/\x27/'\\\x27'/g;
    "'$_'"
  } @_;
  wantarray ? @reply : $reply[0]
}

sub shell_safe_path_or_url($) {
  local $_ = shift;
  return (m{^[A-Za-z0-9._:+/-]+$} and !/^-|^[+]/);
}

# Shell-safety-validating wrapper for File::Temp::tempfile
sub my_tempfile {
  my ($fh, $fn) = tempfile();
  croak "Tempfile name '$fn' not shell-safe; aborting"
        unless shell_safe_path_or_url $fn;
  return ($fh, $fn);
}

# The first argument is a shell script.  Run it and return the shell's
# exit code, and stdout and stderr as references to arrays of lines.
sub run_in_shell($) {
  my $script = shift;
  my $pid = open3 \*SHELL_IN, \*SHELL_OUT, \*SHELL_ERR, qw#/bin/sh#;
  # open3 raises exception when it fails; no need to error check

  print SHELL_IN $script;
  close SHELL_IN;

  # Read loop: tee stdout,stderr to arrays.
  my $select = IO::Select->new(\*SHELL_OUT, \*SHELL_ERR);
  my (@readable, $outlines, $errlines);
  while (@readable = $select->can_read) {
    for my $fh (@readable) {
      my $line = <$fh>;
      $select->remove($fh) if eof $fh or not defined $line;
      next unless defined $line;

      if ($fh == \*SHELL_OUT) {
        push @$outlines, $line;
        print STDOUT $line;
      }
      if ($fh == \*SHELL_ERR) {
        push @$errlines, $line;
        print STDERR $line;
      }
    }
  }
  waitpid $pid, 0; # sets $?
  return $?, $outlines, $errlines;
}


# EXPECTED_ERROR_P is subref called with EXIT_CODE, OUTLINES, ERRLINES,
# expected to return TRUE if the error should be considered fatal (cause
# backport.pl to exit non-zero) or not.  It may be undef for default behaviour.
sub merge {
  my %entry = %{ +shift };
  my $expected_error_p = shift // sub { 0 }; # by default, errors are unexpected
  my $parno = $entry{parno} - scalar grep { $_->{parno} < $entry{parno} } @MERGES_TODAY;

  my ($logmsg_fh, $logmsg_filename) = my_tempfile();
  my (@mergeargs);

  my $shell_escaped_branch = shell_escape($entry{branch})
    if defined($entry{branch});

  if ($entry{branch}) {
    if ($SVNvsn >= 1_008_000) {
      @mergeargs = shell_escape "$BRANCHES/$entry{branch}";
      say $logmsg_fh "Merge $entry{header}:";
    } else {
      @mergeargs = shell_escape qw/--reintegrate/, "$BRANCHES/$entry{branch}";
      say $logmsg_fh "Reintegrate $entry{header}:";
    }
    say $logmsg_fh "";
  } elsif (@{$entry{revisions}}) {
    @mergeargs = shell_escape(
      ($entry{accept} ? "--accept=$entry{accept}" : ()),
      (map { "-c$_" } @{$entry{revisions}}),
      '--',
      '^/subversion/trunk',
    );
    say $logmsg_fh
      "Merge $entry{header} from trunk",
      $entry{accept} ? ", with --accept=$entry{accept}" : "",
      ":";
    say $logmsg_fh "";
  } else {
    die "Don't know how to call $entry{header}";
  }
  say $logmsg_fh $_ for @{$entry{entry}};
  close $logmsg_fh or die "Can't close $logmsg_filename: $!";

  my $reintegrated_word = ($SVNvsn >= 1_008_000) ? "merged" : "reintegrated";
  my $script = <<"EOF";
#!/bin/sh
set -e
if $sh[$DEBUG]; then
  set -x
fi
$SVNq up
$SVNq merge @mergeargs
if [ "`$SVN status -q | wc -l`" -eq 1 ]; then
  if [ -z "`$SVN diff | perl -lne 'print if s/^(Added|Deleted|Modified): //' | grep -vx svn:mergeinfo`" ]; then
    # This check detects STATUS entries that name non-^/subversion/ revnums.
    # ### Q: What if we actually commit a mergeinfo fix to trunk and then want
    # ###    to backport it?
    # ### A: We don't merge it using the script.
    echo "Bogus merge: includes only svn:mergeinfo changes!" >&2
    exit 2
  fi
fi
if $sh[$MAY_COMMIT]; then
  # Remove the approved entry.  The sentinel is important when the entry being
  # removed is the very last one in STATUS, and in that case it has two effects:
  # (1) keeps STATUS from ending in a run of multiple empty lines;
  # (2) makes the \x{7d}k motion behave the same as in all other cases.
  #
  # Use a tempfile because otherwise backport_main() would see the "sentinel paragraph".
  # Since backport_main() has an open descriptor, it will continue to see
  # the STATUS inode that existed when control flow entered backport_main();
  # since we replace the file on disk, when this block of code runs in the
  # next iteration, it will see the new contents.
  cp $STATUS $STATUS.t
  (echo; echo; echo "sentinel paragraph") >> $STATUS.t
  $VIM -e -s -n -N -i NONE -u NONE -c ':0normal! $parno\x{7d}kdap' -c wq $STATUS.t
  $VIM -e -s -n -N -i NONE -u NONE -c '\$normal! dap' -c wq $STATUS.t
  mv $STATUS.t $STATUS
  $SVNq commit -F $logmsg_filename
elif ! $sh[$YES]; then
  echo "Would have committed:"
  echo '[[['
  $SVN status -q
  echo 'M       STATUS (not shown in the diff)'
  cat $logmsg_filename
  echo ']]]'
fi
EOF

  if ($MAY_COMMIT) {
    # STATUS has been edited and the change has been committed
    push @MERGES_TODAY, \%entry;
  }

  $script .= <<"EOF" if $entry{branch};
reinteg_rev=\`$SVN info $STATUS | sed -ne 's/Last Changed Rev: //p'\`
if $sh[$MAY_COMMIT]; then
  # Sleep to avoid out-of-order commit notifications
  if $sh[$YES]; then sleep 15; fi
  $SVNq rm $BRANCHES/$shell_escaped_branch -m "Remove the '"$shell_escaped_branch"' branch, $reintegrated_word in r\$reinteg_rev."
  if $sh[$YES]; then sleep 1; fi
elif ! $sh[$YES]; then
  echo "Would remove $reintegrated_word '"$shell_escaped_branch"' branch"
fi
EOF

  # Include the time so it's easier to find the interesting backups.
  my $backupfile = strftime "backport_pl.%Y%m%d-%H%M%S.$$.tmp", localtime;
  die if -s $backupfile;
  system("$SVN diff > $backupfile") == 0
    or die "Saving a backup diff ($backupfile) failed ($?): $!";
  if (-z $backupfile) {
    unlink $backupfile;
  } else {
    warn "Local mods saved to '$backupfile'\n";
  }

  # If $MAY_COMMIT, then $script will edit STATUS anyway.
  revert(verbose => 0, discard_STATUS => $MAY_COMMIT);

  $MERGED_SOMETHING++;
  my ($exit_code, $outlines, $errlines) = run_in_shell $script;
  unless ($! == 0) {
    die "system() failed to spawn subshell ($!); aborting";
  }
  unless ($exit_code == 0) {
    warn "$0: subshell exited with code $exit_code (in '$entry{header}') "
        ."(maybe due to 'set -e'?)";

    # If we're committing, don't attempt to guess the problem and gracefully
    # continue; just abort.
    if ($MAY_COMMIT) {
      die "Lost track of paragraph numbers; aborting";
    }

    # Record the error, unless the caller wants not to.
    $ERRORS{$entry{id}} = [\%entry, "subshell exited with code $exit_code"]
      unless $expected_error_p->($exit_code, $outlines, $errlines);
  }

  unlink $logmsg_filename unless $exit_code;
}

# Input formats:
#    "1.8.x-r42",
#    "branches/1.8.x-r42",
#    "branches/1.8.x-r42/",
#    "subversion/branches/1.8.x-r42",
#    "subversion/branches/1.8.x-r42/",
#    "^/subversion/branches/1.8.x-r42",
#    "^/subversion/branches/1.8.x-r42/",
# Return value:
#    "1.8.x-r42"
# Works for any branch name that doesn't include slashes.
sub sanitize_branch {
  local $_ = shift;
  s/^\s*//;
  s/\s*$//;
  s#/*$##;
  s#.*/##;
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
  my $parno = shift;
  my @lines = @_;
  my $depends;
  my $accept;
  my (@revisions, @logsummary, $branch, @votes);
  # @lines = @_;

  # strip spaces to match up with the indention
  $_[0] =~ s/^( *)\* //;
  my $indentation = ' ' x (length($1) + 2);
  s/^$indentation// for @_;

  # Ignore trailing spaces: it is not significant on any field, and makes the
  # regexes simpler.
  s/\s*$// for @_;

  # revisions
  $branch = sanitize_branch $1
    and shift
    if $_[0] =~ /^(\S*) branch$/ or $_[0] =~ m#branches/(\S+)#;
  while ($_[0] =~ /^(?:r?\d+[,; ]*)+$/) {
    push @revisions, ($_[0] =~ /(\d+)/g);
    shift;
  }

  # summary
  do {
    push @logsummary, shift
  } until $_[0] =~ /^\s*[A-Z][][\w]*:/ or not defined $_[0];

  # votes
  unshift @votes, pop until $_[-1] =~ /^\s*Votes:/ or not defined $_[-1];
  pop;

  # depends, branch, notes
  # Ignored headers: Changes[*]
  while (@_) {
    given (shift) {
      when (/^Depends:/) {
        $depends++;
      }
      if (s/^Branch:\s*//) {
        $branch = sanitize_branch ($_ || shift || die "Branch header found without value");
      }
      if (s/^Notes:\s*//) {
        my $notes = $_;
        $notes .= shift while @_ and $_[0] !~ /^\w/;
        my %accepts = map { $_ => 1 } ($notes =~ /--accept[ =]([a-z-]+)/g);
        given (scalar keys %accepts) {
          when (0) { }
          when (1) { $accept = [keys %accepts]->[0]; }
          default  {
            warn "Too many --accept values at '",
                 logsummarysummary({ logsummary => [@logsummary] }),
                 "'";
          }
        }
      }
    }
  }

  # Compute a header.
  my ($header, $id);
  if ($branch) {
    $header = "the $branch branch";
    $id = $branch;
  } elsif (@revisions == 1) {
    $header = "r$revisions[0]";
    $id = "r$revisions[0]";
  } elsif (@revisions) {
    $header = "the r$revisions[0] group";
    $id = "r$revisions[0]";
  } else {
    die "Entry '$raw' has neither revisions nor branch";
  }
  my $header_start = ($header =~ /^the/ ? ucfirst($header) : $header);

  warn "Entry has both branch '$branch' and --accept=$accept specified\n"
    if $branch and $accept;

  return (
    revisions => [@revisions],
    logsummary => [@logsummary],
    branch => $branch,
    header => $header,
    header_start => $header_start,
    depends => $depends,
    id => $id,
    votes => [@votes],
    entry => [@lines],
    accept => $accept,
    raw => $raw,
    digest => digest_entry($raw),
    parno => $parno, # $. from backport_main()
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
  my ($fh, $fn) = my_tempfile();
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

  # If $AVAILID is undef, we can only process 'edit' pseudovotes; handle_entry() is
  # supposed to prevent numeric (±1,±0) votes from getting to this point.
  die "Assertion failed" if not defined $AVAILID
                            and grep { $_ ne 'edit' } map { $_->[0] } values %$votes;

  my $had_empty_line;

  $. = 0;
  open STATUS, "<", $STATUS;
  open VOTES, ">", "$STATUS.$$.tmp";
  while (<STATUS>) {
    $had_empty_line = /\n\n\z/;
    my $key = digest_entry $_;

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
      $votesarray[-1]->{digest} = digest_entry $_;
      (exists $approved->{$key}) ? ($raw_approved .= $_) : (print VOTES);
      next;
    }

    s/^(\s*\Q$vote\E:.*)/"$1, $AVAILID"/me
    or s/(.*\w.*?\n)/"$1     $vote: $AVAILID\n"/se;
    $_ = edit_string $_, $entry->{header}, trailing_eol => 2
        if $vote ne '+1';
    $votesarray[-1]->{digest} = digest_entry $_;
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
           : "Vote $_->{vote} on $_->{entry}->{header}$words_vote"
         )
         . "."
         )
      : # exists only in $approved
        "Approve $_->{entry}->{header}."
      } @votesarray;
    (@sentences == 1)
    ? "* STATUS: $sentences[0]"
    : "* STATUS:\n" . join "", map "  $_\n", @sentences;
  };

  system "$SVN diff -- $STATUS";
  printf "[[[\n%s%s]]]\n", $logmsg, ("\n" x ($logmsg !~ /\n\z/));
  if (prompt "Commit these votes? ") {
    my ($logmsg_fh, $logmsg_filename) = my_tempfile();
    print $logmsg_fh $logmsg;
    close $logmsg_fh;
    system("$SVN commit -F $logmsg_filename -- $STATUS") == 0
        or warn("Committing the votes failed($?): $!") and return;
    unlink $logmsg_filename;

    # Add to state votes that aren't '+0' or 'edit'
    $state->{$_->{digest}}++ for grep
                                   +{ qw/-1 t -0 t +1 t/ }->{$_->{vote}},
                                 @votesarray;
  }
}

sub check_local_mods_to_STATUS {
  if (`$SVN status -q $STATUS`) {
    die  "Local mods to STATUS file $STATUS" if $YES;
    warn "Local mods to STATUS file $STATUS";
    system "$SVN diff -- $STATUS";
    prompt "Press the 'any' key to continue...\n", dontprint => 1;
    return 1;
  }
  return 0;
}

sub renormalize_STATUS {
  my $vimscript = <<'EOVIM';
:"" Strip trailing whitespace before entries and section headers, but not
:"" inside entries (e.g., multi-paragraph Notes: fields).
:""
:"" Since an entry is always followed by another entry, section header, or EOF,
:"" there is no need to separately strip trailing whitespace from lines following
:"" entries.
:%s/\v\s+\n(\s*\n)*\ze(\s*[*]|\w)/\r\r/g

:"" Ensure there is exactly one blank line around each entry and header.
:""
:"" First, inject a new empty line above and below each entry and header; then,
:"" squeeze runs of empty lines together.
:0/^=/,$ g/^ *[*]/normal! O
:g/^=/normal! o
:g/^=/-normal! O
:
:%s/\n\n\n\+/\r\r/g

:"" Save.
:wq
EOVIM
  open VIM, '|-', $VIM, qw/-e -s -n -N -i NONE -u NONE --/, $STATUS
    or die "Can't renormalize STATUS: $!";
  print VIM $vimscript;
  close VIM or warn "$0: renormalize_STATUS failed ($?): $!)";

  system("$SVN commit -m '* STATUS: Whitespace changes only.' -- $STATUS") == 0
    or die "$0: Can't renormalize STATUS ($?): $!"
    if $MAY_COMMIT;
}

sub revert {
  my %args = @_;
  die "Bug: \$args{verbose} undefined" unless exists $args{verbose};
  die "Bug: unknown argument" if grep !/^(?:verbose|discard_STATUS)$/, keys %args;

  copy $STATUS, "$STATUS.$$.tmp"        unless $args{discard_STATUS};
  system("$SVN revert -q $STATUS") == 0
    or die "revert failed ($?): $!";
  system("$SVN revert -R ./" . (" -q" x !$args{verbose})) == 0
    or die "revert failed ($?): $!";
  move "$STATUS.$$.tmp", $STATUS        unless $args{discard_STATUS};
  $MERGED_SOMETHING = 0;
}

sub maybe_revert {
  # This is both a SIGINT handler, and the tail end of main() in normal runs.
  # @_ is 'INT' in the former case and () in the latter.
  delete $SIG{INT} unless @_;
  revert verbose => 1 if !$YES and $MERGED_SOMETHING and prompt 'Revert? ';
  (@_ ? exit : return);
}

sub signal_handler {
  my $sig = shift;

  # Clean up after prompt()
  ReadMode 'normal';

  # Fall back to default action
  delete $SIG{$sig};
  kill $sig, $$;
}

sub warning_summary {
  return unless %ERRORS;

  warn "Warning summary\n";
  warn "===============\n";
  warn "\n";
  for my $id (keys %ERRORS) {
    my $title = logsummarysummary $ERRORS{$id}->[0];
    warn "$id ($title): $ERRORS{$id}->[1]\n";
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

# Given an ENTRY, check whether all ENTRY->{revisions} have been merged
# into ENTRY->{branch}, if it has one.  If revisions are missing, record
# a warning in $ERRORS.  Return TRUE If the entry passed the validation
# and FALSE otherwise.
sub validate_branch_contains_named_revisions {
  my %entry = @_;
  return 1 unless defined $entry{branch};
  my %present;

  return "Why are you running so old versions?" # true in boolean context
    if $SVNvsn < 1_005_000;     # doesn't have the 'mergeinfo' subcommand

  my $shell_escaped_branch = shell_escape($entry{branch});
  %present = do {
    my @present = `$SVN mergeinfo --show-revs=merged -- $TRUNK $BRANCHES/$shell_escaped_branch &&
                   $SVN mergeinfo --show-revs=eligible -- $BRANCHES/$shell_escaped_branch`;
    chomp @present;
    @present = map /(\d+)/g, @present;
    map +($_ => 1), @present;
  };

  my @absent = grep { not exists $present{$_} } @{$entry{revisions}};

  if (@absent) {
    $ERRORS{$entry{id}} //= [\%entry,
      sprintf("Revisions '%s' nominated but not included in branch",
              (join ", ", map { "r$_" } @absent)),
    ];
  }
  return @absent ? 0 : 1;
}

sub handle_entry {
  my $in_approved = shift;
  my $approved = shift;
  my $votes = shift;
  my $state = shift;
  my $raw = shift;
  my $parno = shift;
  my $skip = shift;
  my %entry = parse_entry $raw, $parno, @_;
  my @vetoes = grep /^\s*-1:/, @{$entry{votes}};

  my $match = defined($skip) ? ($raw =~ /\Q$skip\E/ or $raw =~ /$skip/msi) : 0
              unless $YES;

  if ($YES) {
    # Run a merge if:
    unless (@vetoes) {
      if ($MAY_COMMIT and $in_approved) {
        # svn-role mode
        merge \%entry if validate_branch_contains_named_revisions %entry;
      } elsif (!$MAY_COMMIT) {
        # Scan-for-conflicts mode

        # First, sanity-check the entry.  We ignore the result; even if it
        # failed, we do want to check for conflicts, in the remainder of this
        # block.
        validate_branch_contains_named_revisions %entry;

        # E155015 is SVN_ERR_WC_FOUND_CONFLICT
        my $expected_error_p = sub {
          my ($exit_code, $outlines, $errlines) = @_;
          ($exit_code == 0)
            or
          (grep /svn: E155015:/, @$errlines)
        };
        merge \%entry, ($entry{depends} ? $expected_error_p : undef);

        my $output = `$SVN status`;

        # Pre-1.6 svn's don't have the 7th column, so fake it.
        $output =~ s/^(......)/$1 /mg if $SVNvsn < 1_006_000;

        my (@conflicts) = ($output =~ m#^(?:C......|.C.....|......C)\s(.*)#mg);
        if (@conflicts and !$entry{depends}) {
          $ERRORS{$entry{id}} //= [\%entry,
                                   sprintf "Conflicts on %s%s%s",
                                     '[' x !!$#conflicts,
                                     (join ', ',
                                      map { basename $_ }
                                      @conflicts),
                                     ']' x !!$#conflicts,
                                  ];
          say STDERR "Conflicts merging $entry{header}!";
          say STDERR "";
          say STDERR $output;
          system "$SVN diff -- " . join ' ', shell_escape @conflicts;
        } elsif (!@conflicts and $entry{depends}) {
          # Not a warning since svn-role may commit the dependency without
          # also committing the dependent in the same pass.
          print "No conflicts merging $entry{header}, but conflicts were "
              ."expected ('Depends:' header set)\n";
        } elsif (@conflicts) {
          say "Conflicts found merging $entry{header}, as expected.";
        }
        revert verbose => 0;
      }
    }
  } elsif (defined($skip) ? not $match : $state->{$entry{digest}}) {
    print "\n\n";
    my $reason = defined($skip) ? "doesn't match pattern"
                                : "remove $STATEFILE to reset";
    say "Skipping $entry{header} ($reason):";
    say logsummarysummary \%entry;
  } elsif ($match or not defined $skip) {
    # This loop is just a hack because 'goto' panics.  The goto should be where
    # the "next PROMPT;" is; there's a "last;" at the end of the loop body.
    PROMPT: while (1) {
    say "";
    say "\n>>> $entry{header_start}:";
    say join ", ", map { "r$_" } @{$entry{revisions}} if @{$entry{revisions}};
    say "$BRANCHES/$entry{branch}" if $entry{branch};
    say "--accept=$entry{accept}" if $entry{accept};
    say "";
    say for @{$entry{logsummary}};
    say "";
    say for @{$entry{votes}};
    say "";
    say "Vetoes found!" if @vetoes;

    # See above for why the while(1).
    QUESTION: while (1) {
    my $key = $entry{digest};
    given (prompt 'Run a merge? [y,l,v,±1,±0,q,e,a, ,N,?] ',
                   verbose => 1, extra => qr/[+-]/) {
      when (/^y/i) {
        # TODO: validate_branch_contains_named_revisions %entry;
        merge \%entry;
        while (1) {
          given (prompt "Shall I open a subshell? [ydN?] ", verbose => 1) {
            when (/^y/i) {
              # TODO: if $MAY_COMMIT, save the log message to a file (say,
              #       backport.logmsg in the wcroot).
              system($SHELL) == 0
                or warn "Creating an interactive subshell failed ($?): $!"
            }
            when (/^d/) {
              system("$SVN diff | $PAGER") == 0
                or warn "diff failed ($?): $!";
              next;
            }
            when (/^[?]/i) {
              print $BACKPORT_OPTIONS_MERGE_OPTIONS_HELP;
              next;
            }
            when (/^N/i) {
              # fall through.
            }
            default {
              next;
            }
          }
          revert verbose => 1;
          next PROMPT;
        }
        # NOTREACHED
      }
      when (/^l/i) {
        if ($entry{branch}) {
            system "$SVN log --stop-on-copy -v -g -r 0:HEAD -- "
                   .shell_escape("$BRANCHES/$entry{branch}")." "
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
      when (/^v/i) {
        say "";
        say for @{$entry{entry}};
        say "";
        next QUESTION;
      }
      when (/^q/i) {
        exit_stage_left $state, $approved, $votes;
      }
      when (/^a/i) {
        $approved->{$key} = \%entry;
        next PROMPT;
      }
      when (/^([+-][01])\s*$/i) {
        next QUESTION if warned_cannot_commit "Entering a vote failed";
        $votes->{$key} = [$1, \%entry];
        say "Your '$1' vote has been recorded." if $VERBOSE;
        last PROMPT;
      }
      when (/^e/i) {
        prompt "Press the 'any' key to continue...\n"
            if warned_cannot_commit "Committing this edit later on may fail";
        my $original = $entry{raw};
        $entry{raw} = edit_string $entry{raw}, $entry{header},
                        trailing_eol => 2;
        # TODO: parse the edited entry (empty lines, logsummary+votes, etc.)
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
      when (/^[?]/i) {
        print $BACKPORT_OPTIONS_HELP;
        next QUESTION;
      }
      default {
        say "Please use one of the options in brackets (q to quit)!";
        next QUESTION;
      }
    }
    last; } # QUESTION
    last; } # PROMPT
  } else {
    # NOTREACHED
    die "Unreachable code reached.";
  }

  1;
}


sub backport_main {
  my %approved;
  my %votes;
  my $state = read_state;
  my $renormalize;

  if (@ARGV && $ARGV[0] eq '--renormalize') {
    $renormalize = 1;
    shift;
  }

  backport_usage, exit 0 if @ARGV > ($YES ? 0 : 1) or grep /^--help$/, @ARGV;
  backport_usage, exit 0 if grep /^(?:-h|-\?|--help|help)$/, @ARGV;
  my $skip = shift; # maybe undef
  # assert not defined $skip if $YES;

  open STATUS, "<", $STATUS or (backport_usage, exit 1);

  # Because we use the ':normal' command in Vim...
  die "A vim with the +ex_extra feature is required for --renormalize and "
      ."\$MAY_COMMIT modes"
      if ($renormalize or $MAY_COMMIT) and `${VIM} --version` !~ /[+]ex_extra/;

  # ### TODO: need to run 'revert' here
  # ### TODO: both here and in merge(), unlink files that previous merges added
  # When running from cron, there shouldn't be local mods.  (For interactive
  # usage, we preserve local mods to STATUS.)
  system("$SVN info $STATUS >/dev/null") == 0
    or die "$0: svn error; point \$SVN to an appropriate binary";

  check_local_mods_to_STATUS;
  renormalize_STATUS if $renormalize;

  # Skip most of the file
  $/ = ""; # paragraph mode
  while (<STATUS>) {
    last if /^Status of \d+\.\d+/;
  }

  $SIG{INT} = \&maybe_revert unless $YES;
  $SIG{TERM} = \&signal_handler unless $YES;

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
      when (/^ *\*/) {
        warn "Too many bullets in $lines[0]" and next
          if grep /^ *\*/, @lines[1..$#lines];
        handle_entry $in_approved, \%approved, \%votes, $state, $lines, $.,
                     $skip,
                     @lines;
      }
      default {
        warn "Unknown entry '$lines[0]'";
      }
    }
  }

  exit_stage_left $state, \%approved, \%votes;
}

sub nominate_main {
  my $had_local_mods;

  local $Text::Wrap::columns = 79;

  $had_local_mods = check_local_mods_to_STATUS;

  # Argument parsing.
  nominate_usage, exit 0 if @ARGV != 2;
  my (@revnums) = (+shift) =~ /(\d+)/g;
  my $justification = shift;

  die "Unable to proceed." if warned_cannot_commit "Nominating failed";

  @revnums = sort { $a <=> $b } keys %{{ map { $_ => 1 } @revnums }};
  die "No revision numbers specified" unless @revnums;

  # Determine whether a backport branch exists
  my ($URL) = `$SVN info` =~ /^URL: (.*)$/m;
  die "Can't retrieve URL of cwd" unless $URL;

  die unless shell_safe_path_or_url $URL;
  system "$SVN info -- $URL-r$revnums[0] 2>/dev/null";
  my $branch = ($? == 0) ? basename("$URL-r$revnums[0]") : undef;

  # Construct entry.
  my $logmsg = `$SVN propget --revprop -r $revnums[0] --strict svn:log '^/'`;
  die "Can't fetch log message of r$revnums[0]: $!" unless $logmsg;

  unless ($logmsg =~ s/^(.*?)\n\n.*/$1/s) {
    # "* file\n  (symbol): Log message."

    # Strip before and after the first symbol's log message.
    $logmsg =~ s/^.*?: //s;
    $logmsg =~ s/^  \x28.*//ms;

    # Undo line wrapping.  (We'll re-do it later.)
    $logmsg =~ s/\s*\n\s+/ /g;
  }

  my @lines;
  warn "Wrapping [$logmsg]\n" if $DEBUG;
  push @lines, wrap " * ", ' 'x3, join ', ', map "r$_", @revnums;
  push @lines, wrap ' 'x3, ' 'x3, split /\n/, $logmsg;
  push @lines, "   Justification:";
  push @lines, wrap ' 'x5, ' 'x5, $justification;
  push @lines, "   Branch: $branch" if defined $branch;
  push @lines, "   Votes:";
  push @lines, "     +1: $AVAILID";
  push @lines, "";
  my $raw = join "", map "$_\n", @lines;

  # Open the file in line-mode (not paragraph-mode).
  my @STATUS;
  tie @STATUS, "Tie::File", $STATUS, recsep => "\n";
  my ($index) = grep { $STATUS[$_] =~ /^Veto/ } (0..$#STATUS);
  die "Couldn't find where to add an entry" unless $index;

  # Add an empty line if needed.
  if ($STATUS[$index-1] =~ /\S/) {
    splice @STATUS, $index, 0, "";
    $index++;
  }

  # Add the entry.
  splice @STATUS, $index, 0, @lines;

  # Save.
  untie @STATUS;

  # Done!
  system "$SVN diff -- $STATUS";
  if (prompt "Commit this nomination? ") {
    system "$SVN commit -m '* STATUS: Nominate r$revnums[0].' -- $STATUS";
    exit $?;
  }
  elsif (!$had_local_mods or prompt "Revert STATUS (destroying local mods)? ") {
    # TODO: we could be smarter and just un-splice the lines we'd added.
    system "$SVN revert -- $STATUS";
    exit $?;
  }

  exit 0;
}

# Dispatch to the appropriate main().
given (basename($0)) {
  when (/^b$|backport/) {
    chdir dirname $0 or die "Can't chdir: $!" if /^b$/;
    &backport_main(@ARGV);
  }
  when (/^n$|nominate/) {
    chdir dirname $0 or die "Can't chdir: $!" if /^n$/;
    &nominate_main(@ARGV);
  }
  default {
    &backport_main(@ARGV);
  }
}
