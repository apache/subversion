#!/usr/bin/perl -w

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

$| = 1;

use strict;
use Carp;
use Cwd;
use Digest::MD5  2.20;
use File::Copy   2.03;
use File::Find;
use File::Path   1.0404;
use File::Temp   0.12   qw(tempdir);
use Getopt::Long 2.25;
use Text::Wrap;
use URI          1.17;

$Text::Wrap::columns = 72;

# Process the command line options.

# The base URL for the portion of the repository to work in.  Note
# that this does not have to be the root of the subversion repository,
# it can point to a subdirectory in the repository.
my $repos_base_url;

# The relative path from the repository base URL to work in to the
# directory to load the input directories into.
my $repos_load_rel_path;

# To specify where tags, which are simply copies of the imported
# directory, should be placed relative to the repository base URL, use
# the -t command line option.  This value must contain regular
# expressions that match portions of the input directory names to
# create an unique tag for each input directory.  The regular
# expressions are surrounded by a specified character to distinguish
# the regular expression from the normal directory path.
my $opt_import_tag_location;

# This is the character used to separate regular expressions occuring
# in the tag directory path from the path itself.
my $REGEX_SEP_CHAR = '@';

# This specifies a configuration file that contains a list of regular
# expressions to check against a file and the properties to set on
# matching files.
my $property_config_filename;

GetOptions('property_cfg_filename=s' => \$property_config_filename,
           'tag_location=s'          => \$opt_import_tag_location)
  or &usage;
&usage("$0: too few arguments") if @ARGV < 3;

$repos_base_url      = shift;
$repos_load_rel_path = shift;

# If the tag directory is set, then the import directory cannot be `.'.
if (defined $opt_import_tag_location and $repos_load_rel_path eq '.')
  {
    &usage("$0: cannot set import_dir to `.' and use -t command line option.");
  }

# Check that the repository base URL, the import and tag directories
# do not contain any ..'s.  Also, the import and tag directories
# cannot be absolute.
if ($repos_base_url =~ /\.{2}/)
  {
    die "$0: repos base URL $repos_base_url cannot contain ..'s.\n";
  }
if ($repos_load_rel_path =~ /\.{2}/)
  {
    die "$0: repos import relative directory path $repos_load_rel_path ",
        "cannot contain ..'s.\n";
  }
if (defined $opt_import_tag_location and $opt_import_tag_location =~ /\.{2}/)
  {
    die "$0: repos tag relative directory path $opt_import_tag_location ",
        "cannot contain ..'s.\n";
  }
if ($repos_load_rel_path =~ m#^/#)
  {
    die "$0: repos import relative directory path $repos_load_rel_path ",
        "cannot start with /.\n";
  }
if (defined $opt_import_tag_location and $opt_import_tag_location =~ m#^/#)
  {
    die "$0: repos tagrelative directory path $opt_import_tag_location ",
        "cannot start with /.\n";
  }

# Convert the string URL into a URI object.
$repos_base_url    =~ s#/*$##;
my $repos_base_uri = URI->new($repos_base_url);

# Check that $repos_load_rel_path is not a directory here implying
# that a command line option was forgotten.
if ($repos_load_rel_path ne '.' and -d $repos_load_rel_path)
  {
    die "$0: import_dir `$repos_load_rel_path' is a directory.\n";
  }

# Determine the native end of line style for this system.  Do this the
# most portable way, by writing a file with a single \n in non-binary
# mode and then reading the file in binary mode.
my $native_eol = &determine_native_eol;

# The remaining command line arguments should be directories.  Check
# that they all exist and that there are no duplicates.
my @load_dirs = @ARGV;
{
  my %dirs;
  foreach my $dir (@load_dirs)
    {
      unless (-e $dir)
        {
          die "$0: directory `$dir' does not exist.\n";
        }

      unless (-d $dir)
        {
          die "$0: directory `$dir' is not a directory.\n";
        }

      if ($dirs{$dir})
        {
          die "$0: directory $dir is listed more than once on command line.\n";
        }
      $dirs{$dir} = 1;
    }
}

# Create the tag locations and print them for the user to review.
# Check that there are no duplicate tags.
my @load_tags;
if (defined $opt_import_tag_location)
  {
    my %seen_tags;

    foreach my $load_dir (@load_dirs)
      {
        # Take the tag relative directory, search for pairs of
        # REGEX_SEP_CHAR's and use the regular expression inside the
        # pair to put in the tag directory name.
        my $tag_location = $opt_import_tag_location;
        my $load_tag     = '';
        while ((my $i = index($tag_location, $REGEX_SEP_CHAR)) >= 0)
          {
            $load_tag .= substr($tag_location, 0, $i, '');
            substr($tag_location, 0, 1, '');
            my $j = index($tag_location, $REGEX_SEP_CHAR);
            if ($j < 0)
              {
                die "$0: -t value `$opt_import_tag_location' does not have ",
                    "matching $REGEX_SEP_CHAR.\n";
              }
            my $regex = substr($tag_location, 0, $j, '');
            $regex = "($regex)" unless ($regex =~ /\(.+\)/);
            substr($tag_location, 0, 1, '');
            my @results = $load_dir =~ m/$regex/;
            $load_tag .= join('', @results);
          }
        $load_tag .= $tag_location;

        print "Directory $load_dir will be tagged as $load_tag\n";

        if ($seen_tags{$load_tag})
          {
            die "$0: duplicate tag generated.\n";
          }
        $seen_tags{$load_tag} = 1;

        push(@load_tags, $load_tag);
      }

    exit 0 unless &get_yes_or_no("Please examine identified tags.  Are they " .
                                 "acceptable? (Y/N) ");
  }

# Load the property configuration filename, if one was specified, into
# an array of hashes, where each hash contains a regular expression
# and a property to apply to the file if the regular expression
# matches.
my @property_settings;
if (defined $property_config_filename and length $property_config_filename)
  {
    open(CFG, $property_config_filename)
      or die "$0: cannot open `$property_config_filename' for reading: $!\n";

    my $ok = 1;

    while (my $line = <CFG>)
      {
        next if $line =~ /^\s*$/;
        next if $line =~ /^\s*#/;

        # Split the input line into words taking into account that
        # single or double quotes may define a single word with
        # whitespace in it.  The format for the file is
        # regex control property_name property_value
        my @line = &split_line($line);
        next if @line == 0;

        unless (@line == 2 or @line == 4)
          {
            warn "$0: line $. of `$property_config_filename' has to have 2 ",
                 "or 4 columns.\n";
            $ok = 0;
            next;
          }
        my ($regex, $control, $property_name, $property_value) = @line;

        unless ($control eq 'break' or $control eq 'cont')
          {
            warn "$0: line $. of `$property_config_filename' has illegal ",
                 "value for column 3 `$control', must be `break' or `cont'.\n";
            $ok = 0;
            next;
          }

        # Compile the regular expression.
        my $re;
        eval { $re = qr/$regex/ };
        if ($@)
          {
            warn "$0: line $. of `$property_config_filename' regex `$regex' ",
                 "does not compile:\n$@\n";
            $ok = 0;
            next;
          }

        push(@property_settings, {name    => $property_name,
                                  value   => $property_value,
                                  control => $control,
                                  re      => $re});
      }
    close(CFG) or
      warn "$0: error in closing `$property_config_filename' for ",
           "reading: $!\n";

    exit 1 unless $ok;
  }

# Check that the svn base URL works by running svn log on it.
read_from_process('svn', 'log', $repos_base_uri);

my $orig_cwd = cwd;

# The first step is to determine the root of the svn repository.  Do
# this with the svn log command.  Take the svn_url hostname and port
# as the initial url and append to it successive portions of the final
# path until svn log succeeds.
my $repos_root_uri;
my $repos_root_uri_path;
my $repos_base_path_segment;
{
  my $r = $repos_base_uri->clone;
  my @path_segments            = grep { length($_) } $r->path_segments;
  my @repos_base_path_segments = @path_segments;
  unshift(@path_segments, '');
  $r->path('');
  my @r_path_segments;

  while (@path_segments)
    {
      $repos_root_uri_path = shift @path_segments;
      push(@r_path_segments, $repos_root_uri_path);
      $r->path_segments(@r_path_segments);
      if (safe_read_from_pipe('svn', 'log', $r) == 0)
        {
          $repos_root_uri = $r;
          last;
        }
      shift @repos_base_path_segments;
    }
  $repos_base_path_segment = join('/', @repos_base_path_segments);
}

if ($repos_root_uri)
  {
    print "Determined that svn root URL is $repos_root_uri.\n";
  }
else
  {
    die "$0: cannot determine root svn URL.\n";
  }

# Create a temporary directory for svn to work in.
my $temp_dir = $ENV{TMPDIR};
unless (defined $temp_dir and length $temp_dir) {
  $temp_dir = '/tmp';
}
my $temp_template = "$temp_dir/svn_load_dirs_XXXXXXXXXX";
$temp_dir         = tempdir($temp_template);

# Create an object that when DESTROY'ed will delete the temporary
# directory.  The CLEANUP flag to tempdir should do this, but they
# call rmtree with 1 as the last argument which takes extra security
# measures that do not clean up the .svn directories.
my $temp_dir_cleanup = Temp::Delete->new;

chdir($temp_dir) or
  die "$0: cannot chdir `$temp_dir': $!\n";

# Now check out the svn repository starting at the svn URL into a
# fixed directory name.
my $checkout_dir_name = 'ZZZ';
print "Checking out $repos_base_uri into $temp_dir/$checkout_dir_name\n";
read_from_process('svn', 'co', $repos_base_uri, $checkout_dir_name);

# Change into the top level directory of the repository and record the
# absolute path to this location because the script will come back
# here.
chdir($checkout_dir_name)
  or die "$0: cannot chdir `$checkout_dir_name': $!\n";
my $wc_top_dir_cwd = cwd;

# Check if all the directories exist to load the directories into the
# repository.  If not, ask if they should be created.  For tags, do
# not create the tag directory itself, that is done on the svn cp.
{
  my @dirs_to_create;
  my %seen_dir;
  my @load_tags_without_last_segment;

  # Assume that the last portion of the tag directory contains the
  # version number and remove it from the directories to create,
  # because the tag directory will be created by svn cp.
  foreach my $load_tag (@load_tags)
    {
      # Skip this tag if there is only one segment in its name.
      next unless $load_tag =~ m#/#;

      # Copy $load_tag otherwise the s/// will modify @load_tags.
      my $l =  $load_tag;
      $l    =~ s#/[^/]*$##;
        push(@load_tags_without_last_segment, $l);
    }
  
  foreach my $dir ($repos_load_rel_path, @load_tags_without_last_segment)
    {
      next unless length $dir;
      my $d = '';
      foreach my $segment (split('/', $dir))
        {
          $d = length $d ? "$d/$segment" : $segment;
          if (!$seen_dir{$d} and ! -d $d)
            {
              push(@dirs_to_create, $d);
            }
          $seen_dir{$d} = 1;
        }
    }

  if (@dirs_to_create)
    {
      print "The following directories do not exist and need to exist:\n";
      foreach my $dir (@dirs_to_create)
        {
          print "  $dir\n";
        }
      exit 0 unless &get_yes_or_no("You must add them now to load the " .
                                   "directories.  Continue (Y/N)? ");

      my $message = "Create directories to load project into.\n\n";

      foreach my $dir (@dirs_to_create)
        {
          mkdir($dir)
            or die "$0: cannot mkdir `$dir': $!\n";
          if (length $repos_base_path_segment)
            {
              $message .= "* $repos_base_path_segment/$dir: New directory.\n";
            }
          else
            {
              $message .= "* $dir: New directory.\n";
            }
        }
      $message = wrap('', '  ', $message);

      read_from_process('svn', 'add', @dirs_to_create);
      read_from_process('svn', 'commit', '-m', $message);
    }
  else
    {
      print "No directories need to be created for loading.\n";
    }
}

# Set up the names for the path to the import and tag directories.
my $repos_load_abs_path;
if ($repos_load_rel_path eq '.')
  {
    $repos_load_abs_path = length($repos_base_path_segment) ?
                           $repos_base_path_segment : "/";
  }
else
  {
    $repos_load_abs_path = length($repos_base_path_segment) ?
                           "$repos_base_path_segment/$repos_load_rel_path" :
                           $repos_load_rel_path;
  }

# Now go through each source directory and copy each file from the
# source directory to the target directory.  For new target files, add
# them to svn.  For files that no longer exist, delete them.
my $wc_import_dir_cwd    = "$wc_top_dir_cwd/$repos_load_rel_path";
my $print_rename_message = 1;
while (@load_dirs)
  {
    my $load_dir = shift @load_dirs;
    my $load_tag = shift @load_tags;
    my $wc_tag_dir_cwd;

    if (defined $load_tag)
      {
        print "\nLoading $load_dir and will save in tag $load_tag.\n";
        $wc_tag_dir_cwd = "$wc_top_dir_cwd/$load_tag";
      }
    else
      {
        print "\nLoading $load_dir.\n";
      }

    # The first hash is keyed by the old name in a rename and the
    # second by the new name.  The last variable contains a list of
    # old and new filenames in a rename.
    my %rename_from_files;
    my %rename_to_files;
    my @renamed_filenames;

    # Loop here until the user has performed all the file and
    # directory renames.
    my $repeat_loop;
    do
      {
        $repeat_loop = 0;

        my %add_files;
        my %del_files;

        # Get the list of files and directories in the repository
        # working copy.  This hash is called %del_files because each
        # file or directory will be deleted from the hash using the
        # list of files and directories in the source directory,
        # leaving the files and directories that need to be deleted.
        %del_files = &recursive_ls_and_hash($wc_import_dir_cwd);

        # This anonymous subroutine finds all the files and
        # directories in the directory to load.  It notes the file
        # type and for each file found, it deletes it from %del_files.
        my $wanted = sub
          {
            s#^\./##;
            return if $_ eq '.';

            my $source_path = $_;
            my $dest_path   = "$wc_import_dir_cwd/$_";

            my $source_type = &file_type($source_path);
            my $dest_type   = &file_type($dest_path);

            # Fail if the destination type exists but is of a
            # different type of file than the source type.
            if ($dest_type ne '0' and $source_type ne $dest_type)
              {
                die "$0: does not handle changing source and destination ",
                  "type for $source_path.\n";
              }

            if ($source_type ne 'f' and $source_type ne 'd')
              {
                die "$0: does not handle copying files of type ",
                    "`$source_type'.\n";
              }

            unless (defined delete $del_files{$source_path})
              {
                $add_files{$source_path}{type} = $source_type;
              }
          };

        # Now change into the directory containing the files to load.
        # First change to the original directory where this script was
        # run so that if the specified directory is a relative
        # directory path, then the script can change into it.
        chdir($orig_cwd)
          or die "$0: cannot chdir `$orig_cwd': $!\n";
        chdir($load_dir)
          or die "$0: cannot chdir `$load_dir': $!\n";

        find({no_chdir   => 1,
              preprocess => sub { sort { $b cmp $a } @_ },
              wanted     => $wanted
             }, '.');

        # At this point %add_files contains the list of new files and
        # directories to be created in the working copy tree and
        # %del_files contains the files and directories that need to
        # be deleted.  Because there may be renames that have taken
        # place, give the user the opportunity to rename any deleted
        # files and directories to ones being added.
        my @add_files = sort keys %add_files;
        my @del_files = sort keys %del_files;

        # Because the source code management system may keep the
        # original renamed file or directory in the working copy until
        # a commit, remove them from the list of deleted files or
        # directories.
        &filter_renamed_files(\@del_files, \%rename_from_files);

        # Now change into the working copy directory in case any
        # renames need to be performed.
        chdir($wc_import_dir_cwd)
          or die "$0: cannot chdir `$wc_import_dir_cwd': $!\n";

        # Only do renames if there are both added and deleted files
        # and directories.
        if (@add_files and @del_files)
          {
            my $max = @add_files > @del_files ? @add_files : @del_files;

            # Print the files that have been added and deleted.  Find
            # the deleted file with the longest name and use that for
            # the width of the filename column.  Add one to the
            # filename width to let the directory / character be
            # appended to a directory name.
            my $line_number_width = 4;
            my $filename_width    = 0;
            foreach my $f (@del_files)
              {
                my $l = length($f);
                $filename_width = $l if $l > $filename_width;
              }
            ++$filename_width;
            my $printf_format = "%${line_number_width}d";

            if ($print_rename_message)
              {
                $print_rename_message = 0;
                print "\n",
                  "The following table lists files and directories that\n",
                  "exist in either the Subversion repository and the\n",
                  "directory to be imported but not both.  You now have the\n",
                  "opportunity to match them up as renames instead of\n",
                  "deletes and adds.  This is a Good Thing as it'll make\n",
                  "the repository take less space.\n\n",
                  "The left column lists files and directories that exist\n",
                  "in the Subversion repository and do not exist in the\n",
                  "directory being imported.  The right column lists files\n",
                  "and directories that exist in the directory being\n",
                  "imported.  Match up a deleted item from the left column\n",
                  "with an added item from the right column.  Note the line\n",
                  "numbers on the the left which you type into this script\n",
                  "to have a rename performed.\n";
              }

            for (my $i=0; $i<$max; ++$i)
              {
                my $add_filename = '';
                my $del_filename = '';
                if ($i < @add_files)
                  {
                    $add_filename = $add_files[$i];
                    if ($add_files{$add_filename}{type} eq 'd')
                      {
                        $add_filename .= '/';
                      }
                  }
                if ($i < @del_files)
                  {
                    $del_filename = $del_files[$i];
                    if ($del_files{$del_filename}{type} eq 'd')
                      {
                        $del_filename .= '/';
                      }
                  }

                if ($i % 22 == 0)
                  {
                    print "\n",
                          " " x $line_number_width,
                          " ",
                          "Deleted", " " x ($filename_width-length("Deleted")),
                          " ",
                          "Added\n";
                  }

                printf $printf_format, $i;
                print  " ", $del_filename,
                       " " x ($filename_width - length($del_filename)),
                       " ", $add_filename, "\n";

                if (($i+1) % 22 == 0)
                  {
                    last unless &get_yes_or_no("Continue printing (Y/N)? ");
                  }
              }

            # Get the feedback from the user.
            my $line;
            my $add_filename;
            my $add_index;
            my $del_filename;
            my $del_index;
            my $got_line = 0;
            do {
              print "Enter two indexes for each column to rename or F when ",
                    "you are finished: ";
              $line = <STDIN>;
              $line = '' unless defined $line;
              if ($line =~ /^F$/i)
                {
                  $got_line = 1;
                }
              elsif ($line =~ /^(\d+)\s+(\d+)$/)
                {
                  $del_index = $1;
                  $add_index = $2;
                  if ($del_index >= @del_files)
                    {
                      print "Delete index $del_index is larger than maximum ",
                            "index of ", scalar @del_files - 1, ".\n";
                      $del_index = undef;
                    }
                  if ($add_index > @add_files)
                    {
                      print "Add index $add_index is larger than maximum ",
                            "index of ", scalar @add_files - 1, ".\n";
                      $add_index = undef;
                    }
                  $got_line = defined $del_index && defined $add_index;

                  # Check that the file or directory to be renamed has
                  # the same file type.
                  if ($got_line)
                    {
                      $add_filename = $add_files[$add_index];
                      $del_filename = $del_files[$del_index];
                      if ($add_files{$add_filename}{type} ne
                          $del_files{$del_filename}{type})
                        {
                          print "File types for $del_filename and ",
                                "$add_filename differ.\n";
                          $got_line = undef;
                        }
                    }
                }
            } until ($got_line);

            if ($line !~ /^F$/i)
              {
                print "Renaming $del_filename to $add_filename.\n";

                $repeat_loop = 1;

                # Because subversion cannot rename the same file or
                # directory twice, which includes doing a rename of a
                # file in a directory that was previously renamed, a
                # commit has to be performed.  Check if the file or
                # directory being renamed now would cause such a
                # problem and commit if so.
                my $do_commit_now = 0;
                foreach my $rename_to_filename (keys %rename_to_files)
                  {
                    if (contained_in($del_filename,
                                     $rename_to_filename,
                                     $rename_to_files{$rename_to_filename}{type}))
                      {
                        $do_commit_now = 1;
                        last;
                      }
                  }

                if ($do_commit_now)
                  {
                    print "Now committing previously run renames.\n";
                    &commit_renames($load_dir,
                                    \@renamed_filenames,
                                    \%rename_from_files,
                                    \%rename_to_files);
                  }

                push(@renamed_filenames, $del_filename, $add_filename);
                $rename_from_files{$del_filename} = $del_files{$del_filename};
                $rename_to_files{$add_filename}   = $del_files{$del_filename};

                # Check that any required directories to do the rename
                # exist.
                my @add_segments = split('/', $add_filename);
                pop(@add_segments);
                my $add_dir = '';
                my @add_dirs;
                foreach my $segment (@add_segments)
                  {
                    $add_dir = length($add_dir) ? "$add_dir/$segment" :
                                                  $segment;
                    unless (-d $add_dir)
                      {
                        push(@add_dirs, $add_dir);
                      }
                  }

                if (@add_dirs)
                  {
                    read_from_process('svn', 'mkdir', @add_dirs);
                  }

                read_from_process('svn', 'mv', $del_filename, $add_filename);

              }
          }
      } while ($repeat_loop);

    # If there are any renames that have not been committed, then do
    # that now.
    if (@renamed_filenames)
      {
        &commit_renames($load_dir,
                        \@renamed_filenames,
                        \%rename_from_files,
                        \%rename_to_files);
      }

    # At this point all renames have been performed.  Now get the
    # final list of files and directories in the working copy
    # directory.  The %add_files hash will contain the list of files
    # and directories to add to the working copy and %del_files starts
    # with all the files already in the working copy and gets files
    # removed that are in the imported directory, which results in a
    # list of files that should be deleted.  %upd_files holds the list
    # of files that have been updated.
    my %add_files;
    my %del_files = &recursive_ls_and_hash($wc_import_dir_cwd);
    my %upd_files;

    # This anonymous subroutine copies files from the source directory
    # to the working copy directory.
    my $wanted = sub
      {
        s#^\./##;
        return if $_ eq '.';

        my $source_path = $_;
        my $dest_path   = "$wc_import_dir_cwd/$_";

        my $source_type = &file_type($source_path);
        my $dest_type   = &file_type($dest_path);

        # Fail if the destination type exists but is of a different
        # type of file than the source type.
        if ($dest_type ne '0' and $source_type ne $dest_type)
          {
            die "$0: does not handle changing source and destination type ",
                "for $source_path.\n";
          }

        # Determine if the file is being added or is an update to an
        # already existing file using the file's digest.
        my $del_info = delete $del_files{$source_path};
        if (defined $del_info)
          {
            if (defined (my $del_digest = $del_info->{digest}))
              {
                my $new_digest = &digest_hash_file($source_path);
                if ($new_digest ne $del_digest)
                  {
                    print "U   $source_path\n";
                    $upd_files{$source_path} = $del_info;
                  }
              }
          }
        else
          {
            print "A   $source_path\n";
            $add_files{$source_path}{type} = $source_type;

            # Create an array reference to hold the list of properties
            # to apply to this object.
            unless (defined $add_files{$source_path}{properties})
              {
                $add_files{$source_path}{properties} = [];
              }

            # Go through the list of properties for a match on this
            # file or directory and if there is a match, then apply
            # the property to it.
            foreach my $property (@property_settings)
              {
                my $re = $property->{re};
                if ($source_path =~ $re)
                  {
                    my $property_name  = $property->{name};
                    my $property_value = $property->{value};

                    # The property value may not be set in the
                    # configuration file, since the user may just want
                    # to set the control flag.
                    if (defined $property_name and defined $property_value)
                      {
                        # Ignore properties that do not apply to
                        # directories.
                        if ($source_type eq 'd')
                          {
                            if ($property_name eq 'svn:eol-style' or
                                $property_name eq 'svn:executable' or
                                $property_name eq 'svn:keywords' or
                                $property_name eq 'svn:mime-type')
                              {
                                next;
                              }
                          }

                        # Ignore properties that do not apply to
                        # files.
                        if ($source_type eq 'f')
                          {
                            if ($property_name eq 'svn:externals' or
                                $property_name eq 'svn:ignore')
                              {
                                next;
                              }
                          }

                        print "Adding to $source_path property ",
                              "`$property_name' with value ",
                              "`$property_value'.\n";

                        push(@{$add_files{$source_path}{properties}},
                             $property);
                      }

                    last if $property->{control} eq 'break';
                  }
              }
          }

        # Now make sure the file or directory in the source directory
        # exists in the repository.
        if ($source_type eq 'd')
          {
            if ($dest_type eq '0')
              {
                mkdir($dest_path)
                  or die "$0: cannot mkdir `$dest_path': $!\n";
              }
          }
        elsif
          ($source_type eq 'f') {
            # Only copy the file if the digests do not match.
            if ($add_files{$source_path} or $upd_files{$source_path})
              {
                copy($source_path, $dest_path)
                  or die "$0: copy `$source_path' to `$dest_path': $!\n";
              }
          }
        else
          {
            die "$0: does not handle copying files of type `$source_type'.\n";
          }
      };

    # Now change into the directory containing the files to load.
    # First change to the original directory where this script was run
    # so that if the specified directory is a relative directory path,
    # then the script can change into it.
    chdir($orig_cwd)
      or die "$0: cannot chdir `$orig_cwd': $!\n";
    chdir($load_dir)
      or die "$0: cannot chdir `$load_dir': $!\n";

    find({no_chdir   => 1,
          preprocess => sub { sort { $b cmp $a } @_ },
          wanted     => $wanted
         }, '.');

    # The files and directories that are in %del_files are the files
    # and directories that need to be deleted.  Because svn will
    # return an error if a file or directory is deleted in a directory
    # that subsequently is deleted, first find all directories and
    # remove from the list any files and directories inside those
    # directories from this list.  Work through the list repeatedly
    # working from short to long names so that directories containing
    # other files and directories will be deleted first.
    do
      {
        $repeat_loop = 0;
        my @del_files = sort {length($a) <=> length($b) || $a cmp $b}
                        keys %del_files;
        &filter_renamed_files(\@del_files, \%rename_from_files);
        foreach my $file (@del_files)
          {
            if ($del_files{$file}{type} eq 'd')
              {
                my $dir        = "$file/";
                my $dir_length = length($dir);
                foreach my $f (@del_files)
                  {
                    next if $file eq $f;
                    if (length($f) >= $dir_length and
                        substr($f, 0, $dir_length) eq $dir)
                      {
                        print "d   $f\n";
                        delete $del_files{$f};
                        $repeat_loop = 1;
                      }
                  }

                # If there were any deletions of files and/or
                # directories inside a directory that will be deleted,
                # then restart the entire loop again, because one or
                # more keys have been deleted from %del_files.
                # Equally important is not to stop this loop if no
                # deletions have been done, otherwise later
                # directories that may contain files and directories
                # to be deleted will not be deleted.
                last if $repeat_loop;
              }
          }
      } while ($repeat_loop);

    # What is left are files that are not in any directories to be
    # deleted and directories to be deleted.  To delete the files,
    # deeper files and directories must be deleted first.  Because we
    # have a hash keyed by remaining files and directories to be
    # deleted, instead of trying to figure out which directories and
    # files are contained in other directories, just reverse sort by
    # the path length and then alphabetically.
    my @del_files = sort {length($b) <=> length($a) || $a cmp $b }
                    keys %del_files;
    &filter_renamed_files(\@del_files, \%rename_from_files);
    foreach my $file (@del_files)
      {
        print "D   $file\n";
      }

    # Now change back to the trunk directory and run the svn commands.
    chdir($wc_import_dir_cwd)
      or die "$0: cannot chdir `$wc_import_dir_cwd': $!\n";

    # If any of the added files have the svn:eol-style property set,
    # then pass -b to diff, otherwise diff may fail because the end of
    # lines have changed and the source file and file in the
    # repository will not be identical.
    my @diff_ignore_space_changes;

    if (keys %add_files)
      {
        my @add_files = sort {length($a) <=> length($b) || $a cmp $b}
                        keys %add_files;
        read_from_process('svn', 'add', @add_files);

        # Add properties on the added files.
        foreach my $add_file (@add_files)
          {
            foreach my $property (@{$add_files{$add_file}{properties}})
              {
                my $property_name  = $property->{name};
                my $property_value = $property->{value};

                if ($property_name eq 'svn:eol-style')
                  {
                    @diff_ignore_space_changes = ('-b');
                  }

                read_from_process('svn', 'propset',
                                  $property_name,
                                  $property_value,
                                  $add_file);
              }
          }
      }
    if (@del_files)
      {
        read_from_process('svn', 'rm', @del_files);
      }

    # Go through the list of updated files and check the svn:eol-style
    # property.  If it is set to native, then convert all CR, CRLF and
    # LF's in the file to the native end of line characters.  Also,
    # modify diff's command line so that it will ignore the change in
    # end of line style.
    if (keys %upd_files)
      {
        my @upd_files = sort {length($a) <=> length($b) || $a cmp $b}
                        keys %upd_files;
        foreach my $upd_file (@upd_files)
          {
            my @command = ('svn', 'propget', 'svn:eol-style', $upd_file);
            my @lines = read_from_process(@command);
            next unless @lines;
            if (@lines > 1)
              {
                warn "$0: `@command' returned more than one line of output: ",
                  "`@lines'.\n";
                next;
              }

            my $eol_style = $lines[0];
            if ($eol_style eq 'native')
              {
                @diff_ignore_space_changes = ('-b');
                if (&convert_file_to_native_eol($upd_file))
                  {
                    print "Native eol-style conversion modified $upd_file.\n";
                  }
              }
          }
      }

    my $message = wrap('', '', "Load $load_dir into $repos_load_abs_path.\n");
    read_from_process('svn', 'commit', '-m', $message);

    # Now remove any files and directories to be deleted in the
    # repository.
    if (@del_files)
      {
        rmtree(\@del_files, 1, 0);
      }

    # Now make the tag by doing a copy in the svn repository itself.
    if (defined $load_tag)
      {
        my $repos_tag_abs_path = length($repos_base_path_segment) ?
                                 "$repos_base_path_segment/$load_tag" :
                                 $load_tag;

        my $from_url = $repos_load_rel_path eq '.' ?
                       $repos_load_rel_path :
                       "$repos_base_url/$repos_load_rel_path";
        my $to_url   = "$repos_base_url/$load_tag";

        $message     = wrap("",
                            "",
                            "Tag $repos_load_abs_path as " .
                            "$repos_tag_abs_path.\n");
        read_from_process('svn', 'cp', '-m', $message, $from_url, $to_url);
      }

    # Finally, go to the top of the svn checked out tree and do an
    # update to pick up the new tag in the working copy.
    chdir($wc_top_dir_cwd)
      or die "$0: cannot chdir `$wc_top_dir_cwd': $!\n";

    read_from_process(qw(svn update));

    # Now run a recursive diff between the original source directory
    # and the tag for a consistency check.
    if (defined $load_tag)
      {
        chdir($orig_cwd)
          or die "$0: cannot chdir `$orig_cwd': $!\n";
        read_from_process('diff', '-u', @diff_ignore_space_changes,
                          '-x', '.svn',
                          '-r', $load_dir, $wc_tag_dir_cwd);
      }
  }

exit 0;

sub usage
{
  warn "@_\n" if @_;
  die "usage: $0 [options] svn_url import_dir dir_v1 [dir_v2 [..]]\n",
    "options are\n",
    "  -p filename  config listing properties to apply to matching files\n",
    "  -t tag_dir   create a tag in tag_dir, which is relative to svn_url\n";
}

sub file_type
{
  lstat(shift) or return '0';
  -b _ and return 'b';
  -c _ and return 'c';
  -d _ and return 'd';
  -f _ and return 'f';
  -l _ and return 'l';
  -p _ and return 'p';
  -S _ and return 'S';
  return '?';
}

# Start a child process safely without using /bin/sh.
sub safe_read_from_pipe
{
  unless (@_)
    {
      croak "$0: safe_read_from_pipe passed no arguments.\n";
    }
  print "Running @_\n";
  my $pid = open(SAFE_READ, '-|');
  unless (defined $pid)
    {
      die "$0: cannot fork: $!\n";
    }
  unless ($pid)
    {
      open(STDERR, ">&STDOUT")
        or die "$0: cannot dup STDOUT: $!\n";
      exec(@_)
        or die "$0: cannot exec `@_': $!\n";
  }
  my @output;
  while (<SAFE_READ>)
    {
      chomp;
      push(@output, $_);
    }
  close(SAFE_READ);
  my $result = $?;
  my $exit   = $result >> 8;
  my $signal = $result & 127;
  my $cd     = $result & 128 ? "with core dump" : "";
  if ($signal or $cd)
    {
      warn "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
    }
  if (wantarray)
    {
      return ($result, @output);
    }
  else
    {
      return $result;
    }
}

# Use safe_read_from_pipe to start a child process safely and exit the
# script if the child failed for whatever reason.
sub read_from_process
{
  unless (@_)
    {
      croak "$0: read_from_process passed no arguments.\n";
    }
  my ($status, @output) = &safe_read_from_pipe(@_);
  if ($status)
    {
      print STDERR "$0: @_ failed with this output:\n", join("\n", @output),
                   "\n",
                   "Press return to quit and clean up svn working directory: ";
      <STDIN>;
      exit 1;
    }
  else
    {
      return @output;
    }
}

# Get a list of all the files and directories in the specified
# directory, the type of file and a digest hash of file types.
sub recursive_ls_and_hash
{
  unless (@_ == 1)
    {
      croak "$0: recursive_ls_and_hash passed incorrect number of ",
            "arguments.\n";
    }

  # This is the directory to change into.
  my $dir = shift;

  # Get the current directory so that the script can change into the
  # current working directory after changing into the specified
  # directory.
  my $return_cwd = cwd;

  chdir($dir)
    or die "$0: cannot chdir `$dir': $!\n";

  my %files;

  my $wanted = sub
    {
      s#^\./##;
      return if $_ eq '.';
      my $file_type = &file_type($_);
      my $file_digest;
      if ($file_type eq 'f' or (stat($_) and -f _))
        {
          $file_digest = &digest_hash_file($_);
        }
      $files{$_} = {type   => $file_type,
                    digest => $file_digest};
    };
  find({no_chdir   => 1,
        preprocess => sub { grep { $_ !~ /\.svn$/ } @_ },
        wanted     => $wanted
       }, '.');

  chdir($return_cwd)
    or die "$0: cannot chdir `$return_cwd': $!\n";

  %files;
}

# Given a list of files and directories which have been renamed but
# not commtited, commit them with a proper log message.
sub commit_renames
{
  unless (@_ == 4)
    {
      croak "$0: commit_renames passed incorrect number of arguments.\n";
    }

  my $load_dir          = shift;
  my $renamed_filenames = shift;
  my $rename_from_files = shift;
  my $rename_to_files   = shift;

  my $number_renames    = @$renamed_filenames/2;

  my $message = "To prepare to load $load_dir into $repos_load_abs_path, " .
                "perform $number_renames rename" .
                ($number_renames > 1 ? "s" : "") . ".\n";

  # Text::Wrap::wrap appears to replace multiple consecutive \n's with
  # one \n, so wrap the text and then append the second \n.
  $message  = wrap("", "", $message) . "\n";
  while (@$renamed_filenames)
    {
      my $from  = "$repos_load_abs_path/" . shift @$renamed_filenames;
      my $to    = "$repos_load_abs_path/" . shift @$renamed_filenames;
      $message .= wrap("", "  ", "* $to: Renamed from $from.\n");
    }

  # Change to the top of the working copy so that any
  # directories will also be updated.
  my $cwd = cwd;
  chdir($wc_top_dir_cwd)
    or die "$0: cannot chdir `$wc_top_dir_cwd': $!\n";
  read_from_process('svn', 'commit', '-m', $message);
  read_from_process('svn', 'update');
  chdir($cwd)
    or die "$0: cannot chdir `$cwd': $!\n";

  # Some versions of subversion have a bug where renamed files
  # or directories are not deleted after a commit, so do that
  # here.
  my @del_files = sort {length($b) <=> length($a) || $a cmp $b }
                  keys %$rename_from_files;
  rmtree(\@del_files, 1, 0);

  # Empty the list of old and new renamed names.
  undef %$rename_from_files;
  undef %$rename_to_files;
}

# Take a one file or directory and see if its name is equal to a
# second or is contained in the second if the second file's file type
# is a directory.
sub contained_in
{
  unless (@_ == 3)
    {
      croak "$0: contain_in passed incorrect number of arguments.\n";
    }

  my $contained      = shift;
  my $container      = shift;
  my $container_type = shift;

  if ($container eq $contained)
    {
      return 1;
    }

  if ($container_type eq 'd')
    {
      my $dirname        = "$container/";
      my $dirname_length = length($dirname);

      if ($dirname_length <= length($contained) and
          $dirname eq substr($contained, 0, $dirname_length))
        {
          return 1;
        }
    }

  return 0;
}

# Take an array reference containing a list of files and directories
# and take a hash reference and remove from the array reference any
# files and directories and the files the directory contains listed in
# the hash.
sub filter_renamed_files
{
  unless (@_ == 2)
    {
      croak "$0: filter_renamed_files passed incorrect number of arguments.\n";
    }

  my $array_ref = shift;
  my $hash_ref  = shift;

  foreach my $remove_filename (keys %$hash_ref)
    {
      my $remove_file_type = $hash_ref->{$remove_filename}{type};
      for (my $i=0; $i<@$array_ref;)
        {
          if (contained_in($array_ref->[$i],
                           $remove_filename,
                           $remove_file_type))
            {
              splice(@$array_ref, $i, 1);
              next;
            }
          ++$i;
        }
    }
}

# Get a digest hash of the specified filename.
sub digest_hash_file
{
  unless (@_ == 1)
    {
      croak "$0: digest_hash_file passed incorrect number of arguments.\n";
    }

  my $filename = shift;

  my $ctx = Digest::MD5->new;
  if (open(READ, $filename))
    {
      binmode(READ);
      $ctx->addfile(*READ);
      close(READ);
    }
  else
    {
      die "$0: cannot open `$filename' for reading: $!\n";
    }
  $ctx->digest;
}

# Read standard input until a line contains either a upper or
# lowercase y or n.
sub get_yes_or_no
{
  unless (@_ == 1)
    {
      croak "$0: get_yes_or_no passed incorrect number of arguments.\n";
    }

  my $message = shift;

  my $line;
  do
    {
      print $message;
      $line = <STDIN>;
      $line = '' unless defined $line;
    } until $line =~ /[yn]/i;
  $line =~ /y/i;
}

# Determine the native end of line on this system by writing a \n in
# non-binary mode to an empty file and reading the same file back in
# binary mode.
sub determine_native_eol
{
  my $filename = "svn_load_dirs_eol_test.$$";
  if (-e $filename)
    {
      unlink($filename)
        or die "$0: cannot unlink `$filename': $!\n";
    }
  open(NL_TEST, ">$filename")
    or die "$0: cannot open `$filename' for writing: $!\n";
  print NL_TEST "\n";
  close(NL_TEST) or
    die "$0: error in closing `$filename' for writing: $!\n";
  open(NL_TEST, $filename)
    or die "$0: cannot open `$filename' for reading: $!\n";
  local $/;
  undef $/;
  my $eol = <NL_TEST>;
  close(NL_TEST)
    or die "$0: cannot close `$filename' for reading: $!\n";
  unlink($filename) or
    die "$0: cannot unlink `$filename': $!\n";

  my $eol_length = length($eol);
  unless ($eol_length)
    {
      die "$0: native eol length on this system is 0.\n";
    }

  print "Native EOL on this system is ";
  for (my $i=0; $i<$eol_length; ++$i)
    {
      printf "\\%03o", ord(substr($eol, $i, 1));
    }
  print ".\n";

  $eol;
}

# Take a filename, open the file and replace all CR, CRLF and LF's
# with the native end of line style for this system.
sub convert_file_to_native_eol
{
  unless (@_ == 1)
    {
      croak "$0: convert_file_to_native_eol passed incorrect number of ",
            "arguments.\n";
    }

  my $filename = shift;
  open(FILE, $filename)
    or die "$0: cannot open `$filename' for reading: $!\n";
  binmode FILE;
  local $/;
  undef $/;
  my $in = <FILE>;
  close(FILE)
    or die "$0: error in closing `$filename' for reading: $!\n";
  my $out = '';

  # Go through the file and transform it byte by byte.
  my $i = 0;
  while ($i < length($in))
    {
      my $cc = substr($in, $i, 2);
      if ($cc eq "\015\012")
        {
          $out .= $native_eol;
          $i += 2;
          next;
        }

      my $c = substr($cc, 0, 1);
      if ($c eq "\012" or $c eq "\015")
        {
          $out .= $native_eol;
        }
      else
        {
          $out .= $c;
        }
      ++$i;
    }

  return 0 if $in eq $out;

  my $tmp_filename = ".svn/tmp/svn_load_dirs.$$";
  open(FILE, ">$tmp_filename")
    or die "$0: cannot open `$tmp_filename' for writing: $!\n";
  binmode FILE;
  print FILE $out;
  close(FILE)
    or die "$0: cannot close `$tmp_filename' for writing: $!\n";
  rename($tmp_filename, $filename)
    or die "$0: cannot rename `$tmp_filename' to `$filename': $!\n";

  return 1;
}

# Split the input line into words taking into account that single or
# double quotes may define a single word with whitespace in it.
sub split_line
{
  unless (@_ == 1)
    {
      croak "$0: split_line passed incorrect number of arguments.\n";
    }

  my $line = shift;

  # Strip leading whitespace.  Do not strip trailing whitespace which
  # may be part of quoted text that was never closed.
  $line =~ s/^\s+//;

  my $line_length  = length $line;
  my @words        = ();
  my $current_word = '';
  my $in_quote     = '';
  my $in_protect   = '';
  my $in_space     = '';
  my $i            = 0;

  while ($i < $line_length)
    {
      my $c = substr($line, $i, 1);
      ++$i;

      if ($in_protect)
        {
          if ($c eq $in_quote)
            {
              $current_word .= $c;
            }
          elsif ($c eq '"' or $c eq "'")
            {
              $current_word .= $c;
            }
          else
            {
              $current_word .= "$in_protect$c";
            }
          $in_protect = '';
        }
      elsif ($c eq '\\')
        {
          $in_protect = $c;
        }
      elsif ($in_quote)
        {
          if ($c eq $in_quote)
            {
              $in_quote = '';
            }
          else
            {
              $current_word .= $c;
            }
        }
      elsif ($c eq '"' or $c eq "'")
        {
          $in_quote = $c;
        }
      elsif ($c =~ m/^\s$/)
        {
          unless ($in_space)
            {
              push(@words, $current_word);
              $current_word = '';
            }
        }
      else
        {
          $current_word .= $c;
        }

      $in_space = $c =~ m/^\s$/;
    }

  # Handle any leftovers.
  $current_word .= $in_protect if $in_protect;
  push(@words, $current_word) if length $current_word;

  @words;
}

# This package exists just to delete the temporary directory.
package Temp::Delete;

sub new
{
  bless {}, shift;
}

sub DESTROY
{
  print "Cleaning up $temp_dir\n";
  File::Path::rmtree([$temp_dir], 0, 0);
}

# local variables:
# eval: (load-file "../dev/svn-dev.el")
# end:
