#!/usr/bin/perl -w

# A script that allows some simple testing of Subversion, in
# particular concurrent read, write and read-write access by the 'svn'
# client. It can also create working copy trees containing a large
# number of files and directories. All repository access is via the
# 'svnadmin' and 'svn' commands.
#
# This script constructs a repository, and populates it with
# files. Then it loops making changes to a subset of the files and
# committing the tree. Thus when two, or more, instances are run in
# parallel there is concurrent read and write access. Sometimes a
# commit will fail due to a commit conflict. This is expected, and is
# automatically resolved by updating the working copy.
#
# Each file starts off containing:
#    A0
#    0
#    A1
#    1
#    A2
#    .
#    .
#    A9
#    9
#
# The script runs with an ID in the range 0-9, and when it modifies a
# file it modifes the line that starts with its ID. Thus scripts with
# different IDs will make changes that can be merged automatically.
#
# The main loop is then:
#
#   step 1: modify a random selection of files
#
#   step 2: optional sleep or wait for RETURN keypress
#
#   step 3: update the working copy automatically merging out-of-date files
#
#   step 4: try to commit, if not successful go to step 3 otherwise go to step 1
#
# To allow break-out of potentially infinite loops, the script will
# terminate if it detects the presence of a "stop file", the path to
# which is specified with the -S option (default ./stop). This allows
# the script to be stopped without any danger of interrupting an 'svn'
# command, which experiment shows may require Berkeley db_recover to
# be used on the repository.
#
#  Running the Script
#  ==================
#
# Use three xterms all with shells on the same directory.  In the
# first xterm run (note, this will remove anything called repostress
# in the current directory)
#
#         % stress.pl -c -s1
#
# When the message "Committed revision 1." scrolls pass use the second
# xterm to run
#
#         % stress.pl -s1
#
# Both xterms will modify, update and commit separate working copies to
# the same repository.
#
# Use the third xterm to touch a file 'stop' to cause the scripts to
# exit cleanly, i.e. without interrupting an svn command.
#
# To run a third, fourth, etc. instance of the script use -i
#
#         % stress.pl -s1 -i2
#         % stress.pl -s1 -i3
#
# Running several instances at once will cause a *lot* of disk
# activity. I have run ten instances simultaneously on a Linux tmpfs
# (RAM based) filesystem -- watching ten xterms scroll irregularly
# can be quite hypnotic!


use Getopt::Std;
use File::Find;
use File::Path;
use Cwd;

# Repository check/create
sub init_repo
  {
    my ( $repo, $create ) = @_;
    if ( $create )
      {
	rmtree([$repo]) if -e $repo;
	my $svnadmin_cmd = "svnadmin create $repo";
	system( $svnadmin_cmd) and die "$svnadmin_cmd: failed: $?\n";
      }
    else
      {
	my $svnadmin_cmd = "svnadmin youngest $repo";
	my $revision = readpipe $svnadmin_cmd;
	die "$svnadmin_cmd: failed\n" if not $revision =~ m{^[0-9]};
      }
    $repo = getcwd . "/$repo" if not $repo =~ m[^/];
    return $repo;
  }

# Check-out a working copy
sub check_out
  {
    my ( $url ) = @_;
    my $wc_dir = "wcstress.$$";
    mkdir "$wc_dir", 0755 or die "mkdir wcstress.$$: $!\n";
    my $svn_cmd = "svn co $url $wc_dir";
    system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";
    return $wc_dir;
  }

# Print status and update. The update is to do any required merges.
sub status_update
  {
    my ( $wc_dir, $wait_for_key ) = @_;
    my $svn_cmd = "svn st -u $wc_dir";
    print "Status:\n";
    system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";
    print "Press return to update/commit\n" if $wait_for_key;
    read STDIN, $wait_for_key, 1 if $wait_for_key;
    print "Updating:\n";
    $svn_cmd = "svn up $wc_dir";
    system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";
  }

# Print status, update and commit. The update is to do any required
# merges.  Returns 0 if the commit succeeds and 1 if it fails due to a
# conflict.
sub status_update_commit
  {
    my ( $wc_dir, $wait_for_key ) = @_;
    status_update $wc_dir, $wait_for_key;
    print "Committing:\n";
    # Use current time as log message
    my $now_time = localtime;
    my $svn_cmd = "svn ci $wc_dir -m '$now_time'";

    # Need to handle the commit carefully. It could fail for all sorts
    # of reasons, but errors that indicate a conflict are "acceptable"
    # while other errors are not.  Thus there is a need to check the
    # return value and parse the error text.

    pipe COMMIT_ERR_READ, COMMIT_ERR_WRITE or die "pipe: $!\n";
    my $pid = fork();
    die "fork failed: $!\n" if not defined $pid;
    if ( not $pid )
      {
	# This is the child process
	open( STDERR, ">&COMMIT_ERR_WRITE" ) or die "redirect failed: $!\n";
	exec $svn_cmd or die "exec $svn_cmd failed: $!\n";
      }

    # This is the main parent process, look for acceptable errors
    close COMMIT_ERR_WRITE or die "close COMMIT_ERR_WRITE: $!\n";
    my $acceptable_error = 0;
    while ( <COMMIT_ERR_READ> )
      {
	print STDERR;
	$acceptable_error = 1 if ( /^svn:[ ]
				   (
				    Transaction[ ]is[ ]out[ ]of[ ]date
				    |
				    Merge[ ]conflict[ ]during[ ]commit
				    |
				    Baseline[ ]incorrect
				   )
				   $/x );
      }
    close COMMIT_ERR_READ or die "close COMMIT_ERR_READ: $!\n";

    # Get commit subprocess exit status
    die "waitpid: $!\n" if $pid != waitpid $pid, 0;
    die "unexpected commit fail: exit status: $?\n"
      if ( $? != 0 and $? != 256 ) or ( $? == 256 and $acceptable_error != 1 );

    return $? == 256 ? 1 : 0;
  }

# Get a list of all versioned files in the working copy
{
  my @get_list_of_files_helper_array;
  sub GetListOfFilesHelper
    {
      $File::Find::prune = 1 if $File::Find::name =~ m[/.svn];
      return if $File::Find::prune or -d;
      push @get_list_of_files_helper_array, $File::Find::name;
    }
  sub GetListOfFiles
    {
      my ( $wc_dir ) = @_;
      @get_list_of_files_helper_array = ();
      find( \&GetListOfFilesHelper, $wc_dir);
      return @get_list_of_files_helper_array;
    }
}

# Populate a working copy
sub populate
  {
    my ( $dir, $dir_width, $file_width, $depth ) = @_;
    return if not $depth--;

    for $nfile ( 1..$file_width )
      {
	my $filename = "$dir/foo$nfile";
	open( FOO, ">$filename" ) or die "open $filename: $!\n";

	for $line ( 0..9 )
	  {
	    print FOO "A$line\n$line\n" or die "write to $filename: $!\n";
	  }
	close FOO or die "close $filename: $!\n";

	my $svn_cmd = "svn add $filename";
	system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";
      }

    if ( $depth )
      {
	for $ndir ( 1..$dir_width )
	  {
	    my $dirname = "$dir/bar$ndir";
	    my $svn_cmd = "svn mkdir $dirname";
	    system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";

	    populate( "$dirname", $dir_width, $file_width, $depth );
	  }
      }
  }

# Modify a versioned file in the working copy
sub ModFile
  {
    my ( $filename, $mod_number, $id ) = @_;

    # Read file into memory replacing the line that starts with our ID
    open( FOO, "<$filename" ) or die "open $filename: $!\n";
    @lines = map { s[(^$id.*)][$1,$mod_number]; $_ } <FOO>;
    close FOO or die "close $filename: $!\n";

    # Write the memory back to the file
    open( FOO, ">$filename" ) or die "open $filename: $!\n";
    print FOO or die "print $filename: $!\n" foreach @lines;
    close FOO or die "close $filename: $!\n";
  }

sub ParseCommandLine
  {
    my %cmd_opts;
    my $usage = "
usage: stress.pl [-c] [-i num] [-n num] [-s secs] [-x num]
                 [-D num] [-F num] [-N num] [-R path] [-S path] [-U url]
where
  -c cause repository creation
  -i the ID (valid IDs are 0 to 9, default is 0 if -c given, 1 otherwise)
  -n the number of sets of changes to commit
  -s the sleep delay (-1 wait for key, 0 none)
  -x the number of files to modify in each commit
  -D the number of sub-directories per directory in the tree
  -F the number of files per directory in the tree
  -N the depth of the tree
  -R the path to the repository
  -S the path to the file whose presence stops this script
  -U the URL to the repository (file:///<-R path> by default)
";

    # defaults
    $cmd_opts{'D'} = 2;            # number of subdirs per dir
    $cmd_opts{'F'} = 2;            # number of files per dir
    $cmd_opts{'N'} = 2;            # depth
    $cmd_opts{'R'} = "repostress"; # repository name
    $cmd_opts{'S'} = "stop";       # path of file to stop the script
    $cmd_opts{'U'} = "none";       # URL
    $cmd_opts{'c'} = 0;            # create repository
    $cmd_opts{'i'} = 0;            # ID
    $cmd_opts{'s'} = -1;           # sleep interval
    $cmd_opts{'n'} = 200;          # sets of changes
    $cmd_opts{'x'} = 4;            # files to modify

    getopts( 'ci:n:s:x:D:F:N:R:U:', \%cmd_opts ) or die $usage;

    # default ID if not set
    $cmd_opts{'i'} = 1 - $cmd_opts{'c'} if not $cmd_opts{'i'};

    die $usage if $cmd_opts{'i'} !~ /^[0-9]$/;

    return %cmd_opts;
  }

############################################################################
# Main

srand 123456789;

my %cmd_opts = ParseCommandLine();

my $repo = init_repo $cmd_opts{'R'}, $cmd_opts{'c'};

# Make URL from path if URL not explicitly specified
$cmd_opts{'U'} = "file://$repo" if $cmd_opts{'U'} eq "none";

my $wc_dir = check_out $cmd_opts{'U'};

if ( $cmd_opts{'c'} )
  {
    my $svn_cmd = "svn mkdir $wc_dir/trunk";
    system( $svn_cmd ) and die "$svn_cmd: failed: $?\n";
    populate "$wc_dir/trunk", $cmd_opts{'D'}, $cmd_opts{'F'}, $cmd_opts{'N'};
    status_update_commit $wc_dir, 0 and die "populate checkin failed\n";
  }

my @wc_files = GetListOfFiles $wc_dir;
die "not enough files in repository\n" if $#wc_files + 1 < $cmd_opts{'x'};

my $wait_for_key = $cmd_opts{'s'} < 0;

my $stop_file = $cmd_opts{'S'};

for $mod_number ( 1..$cmd_opts{'n'} )
  {
    my @chosen;
    for ( 1..$cmd_opts{'x'} )
      {
	# Extract random file from list and modify it
	my $mod_file = splice @wc_files, int rand $#wc_files, 1;
	ModFile $mod_file, $mod_number, $cmd_opts{'i'};
	push @chosen, $mod_file;
      }
    # Reinstate list of files, the order doesn't matter
    push @wc_files, @chosen;

    if ( $cmd_opts{'x'} > 0 ) {
      # Loop committing until successful or the stop file is created
      1 while not -e $stop_file and status_update_commit $wc_dir, $wait_for_key;
    } else {
      status_update $wc_dir, $wait_for_key;
    }

    # Break out of loop, or sleep, if required
    print( "stop file '$stop_file' detected\n" ), last if -e $stop_file;
    sleep $cmd_opts{'s'} if $cmd_opts{'s'} > 0;
  }
