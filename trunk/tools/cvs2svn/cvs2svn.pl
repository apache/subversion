#!/usr/bin/perl -w

use strict;

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsTime;
use Carp;
use Time::Local;

@CvsTime::ISA = ('CheckedClass');

sub CvsTime::type_info {
    return {
	optional_args => {
	    STRING      => 'SCALAR',
	    TIME_T      => 'SCALAR',
	},
    };
}

sub CvsTime::initialize {
    my ($self) = @_;
    if (exists $self->{STRING}) {
	my $string = \$self->{STRING};
	my ($year, $mon, $mday, $hours, $min, $secs) =
	    $$string =~ /^(\d+)\.(\d+)\.(\d+)\.(\d+)\.(\d+)\.(\d+)$/
		or croak "malformed CVS time string \"$$string\"";

	# Assume two digit year is in the 20th century.
	if ($year >= 69 and $year <= 99) {
	    $$string = "19$$string";
	    $year += 1900;
	}

	$self->{TIME_T} =
	    timegm($secs, $min, $hours, $mday, $mon - 1, $year - 1900);
    } elsif (exists $self->{TIME_T}) {
	my ($secs, $min, $hours, $mday, $mon, $year) = gmtime($self->{TIME_T});
	$mon++;
	$year += 1900;
	$self->{STRING} = join '.', ($year, $mon, $mday, $hours, $min, $secs);
    } else {
	die "CvsTime constructor must specify either STRING or TIME_T";
    }	
}

sub CvsTime::time_t {
    my ($self) = @_;
    return $self->{TIME_T};
}

sub CvsTime::compare_to {
    my ($self, $peer) = @_;
    $self->check;
    croak "CvsTime can't compare to $peer" unless $peer->isa('CvsTime');
    return $self->{TIME_T} <=> $peer->{TIME_T};
}

sub CvsTime::self_test {
    my $time0 = CvsTime->new(STRING => '1998.10.31.12.15.54');
    my $time1 = $time0->new(STRING => '98.10.31.12.16.54');

    my $time2 = CvsTime->new(TIME_T => $time0->time_t);
    die unless $time2->compare_to($time0) == 0;

    die if (eval { CvsTime->new(STRING => '1998.10.31.12.15') })
	or $@ !~ /^malformed/;
    die if (eval { CvsTime->new(STRING => '1998.10.31.12.15.54.32') })
	or $@ !~ /^malformed/;

    die unless $time0->compare_to($time1) < 0;
    die unless $time1->compare_to($time0) > 0;
    die unless $time0->compare_to($time0) == 0;

    die if (eval { $time0->compare_to(bless { }, 'Foo') })
	or $@ !~ /CvsTime can\'t compare to Foo/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsRevID;
use Carp;

@CvsRevID::ISA = ('CheckedClass');

sub CvsRevID::type_info {
    return {
	required_args => {
	    STRING      => 'SCALAR',
	},
	members => {
	    TUPLE      => 'ARRAY',
	},
    };
}

sub CvsRevID::initialize {
    my ($self) = @_;
    my ($string) = $self->{STRING};
    croak "malformed CVS rev ID \"$string\""
	unless $string =~ /^\d+(\.\d+)*$/;
    $self->{TUPLE} = [ split /\./, $string ];
}

sub CvsRevID::self_test {
    my $id0 = CvsRevID->new(STRING => '1.4.2.3');

    die if (eval { CvsRevID->new(STRING => '1..2') })
	or $@ !~ /^malformed/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsName;		# abstract superclass for named things in CVS
use Carp;

@CvsName::ISA = ('CheckedClass');

sub CvsName::type_info {
    return {
	required_args => {
	    NAME => 'SCALAR',
	},
	members => {
	    REVS => 'ARRAY',
	},
    };
}

sub CvsName::name {
    my ($self) = @_;
    return $self->{NAME};
}

sub CvsName::add_rev {
    my ($self, $rev) = @_;
    croak "CvsName::add_rev: $rev is not a CvsFileRev\n"
	unless $rev->isa('CvsFileRev');
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub CvsName::self_test {
    my $cn0 = CvsName->new(NAME => 'foo');
    die unless $cn0->name eq 'foo';

    $cn0->add_rev(bless { }, 'CvsFileRev');
    $cn0->add_rev(bless { }, 'CvsFileRev');
    die unless @{$cn0->{REVS}} == 2;

    die if (eval { $cn0->add_rev(bless { }, 'Foo') })
	or $@ !~ /^CvsName::add_rev: Foo=HASH\(.*\) is not a CvsFileRev/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsAuthor;
use Carp;

@CvsAuthor::ISA = ('CvsName');

sub CvsAuthor::self_test {
    my $author = CvsAuthor->new(NAME => 'jrandom');
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsTag;
use Carp;

@CvsTag::ISA = ('CvsName');

sub CvsTag::self_test {
    my $tag = CvsTag->new(NAME => 'beta3_2');
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsLog;
use Carp;

@CvsLog::ISA = ('CvsName');

sub CvsLog::self_test {
    my $log = CvsLog->new(NAME => 'I checked it in.');
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsFileRev;
use Carp;

@CvsFileRev::ISA = ('CheckedClass');

sub CvsFileRev::type_info {
    return {
	required_args => {
	    REV         => 'CvsRevID',
	    FILE        => 'CvsFile',
	    AUTHOR      => 'CvsAuthor',
	    TIME        => 'CvsTime',
	    LOG         => 'CvsLog',
	},
	optional_args => {
	    PREDECESSOR => 'CvsFileRev', # undefined for initial rev.
	    TAGS        => 'ARRAY',
	    STATE       => 'SCALAR',
	},
    };
}

sub CvsFileRev::get_file {
    my ($self) = @_;
    return $self->{FILE};
}

sub CvsFileRev::get_author {
    my ($self) = @_;
    return $self->{AUTHOR};
}

sub CvsFileRev::get_time {
    my ($self) = @_;
    return $self->{TIME};
}

sub CvsFileRev::get_log {
    my ($self) = @_;
    return $self->{LOG};
}

sub CvsFileRev::add_tag {
    my ($self, $tag) = @_;
    croak "$tag is not a CvsTag" unless $tag->isa('CvsTag');
    push @{$self->{TAGS}}, $tag;
    $self->check;
}

sub CvsFileRev::self_test {
    my $file0 = bless { }, 'CvsFile';
    my $author0 = bless { }, 'CvsAuthor';
    my $time0 = bless { }, 'CvsTime';
    my $log0 = bless { }, 'CvsLog';
    my $fr0 = CvsFileRev->new(
			      REV    => (bless { }, 'CvsRevID'),
			      FILE   => $file0,,
			      AUTHOR => $author0,
			      TIME   => $time0,
			      LOG    => $log0,
			      );
    $fr0->add_tag(bless { }, 'CvsTag');

    die unless $fr0->get_author == $author0;
    die unless $fr0->get_time == $time0;
    die unless $fr0->get_file == $file0;
    die unless $fr0->get_log == $log0;

    die if (eval { $fr0->add_tag(bless { }, 'Foo') })
	or $@ !~ /^Foo=HASH\(0x[0-9a-f]+\) is not a CvsTag/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsDirEntry;
use Carp;

@CvsDirEntry::ISA = ('CheckedClass');

sub CvsDirEntry::type_info {
    return {
	required_args => {
	    NAME => 'SCALAR',
	},
	optional_args => {
	    PARENT_DIR => 'CvsDir',
	},
    };
}

sub CvsDirEntry::name {
    my ($self) = @_;
    return $self->{NAME};
}

sub CvsDirEntry::path {
    my ($self) = @_;
    return $self->{NAME} unless $self->{PARENT_DIR};
    return $self->{PARENT_DIR}->path . "/" . $self->{NAME};
			       
}

sub CvsDirEntry::set_parent {
    my ($self, $parent) = @_;
    $self->{PARENT_DIR} = $parent;
    $self->check;
}

sub CvsDirEntry::self_test {
    my $de0 = CvsDirEntry->new(NAME => 'slug');
    die unless $de0->name eq 'slug';
    die unless $de0->path eq 'slug';

    my $de1 = CvsDirEntry->new(NAME => 'grub');
    $de1->set_parent(bless $de0, 'CvsDir');
    die unless $de1->name eq 'grub';
    die unless $de1->path eq 'slug/grub';
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsFile;
use Carp;

@CvsFile::ISA = ('CvsDirEntry');

sub CvsFile::type_info {
    return {
	members => {
	    REVS => 'ARRAY',
	},
    };
}

sub CvsFile::count_dirs {
    return 0;
}

sub CvsFile::count_files {
    # print "FILE: $_[0]->{NAME}\n";
    return 1;
}

sub CvsFile::count_filerevs {
    my ($self) = @_;
    return 0 + $self->filerevs;
}

sub CvsFile::filerevs {
    my ($self) = @_;
    return my @empty = () unless $self->{REVS};
    return @{$self->{REVS}};
}

sub CvsFile::add_filerev {
    my ($self, $rev) = @_;
    croak "$rev is not a CvsFileRev" unless $rev->isa('CvsFileRev');
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub CvsFile::self_test {
    my $f0 = CvsFile->new(NAME => 'hello.c');
    my $f1 = CvsFile->new(NAME => 'Makefile');

    my $rev0 = bless { }, 'CvsFileRev';
    my $rev1 = bless { }, 'CvsFileRev';
    $f0->add_filerev($rev0);
    $f0->add_filerev($rev1);

    die unless $f0->count_dirs == 0;
    die unless $f0->count_files == 1;
    die unless $f0->count_filerevs == 2;

    die unless $f1->count_dirs == 0;
    die unless $f1->count_files == 1;
    die unless $f1->count_filerevs == 0;

    die unless ($f0->filerevs)[0] == $rev0;
    die unless ($f0->filerevs)[1] == $rev1;

    die if (eval { $f0->add_filerev(bless { }, 'Foo') })
	or $@ !~ /^Foo=HASH\(.*\) is not a CvsFileRev/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsDir;
use Carp;

@CvsDir::ISA = ('CvsDirEntry');

sub CvsDir::type_info {
    return {
	members => {
	    ENTRIES => 'HASH',
	},
    };
}

sub CvsDir::count_dirs {
    my ($self) = @_;
    my $count = 1;		# this dir
    # print "DIR: $self->{NAME}\n";
    for (values %{$self->{ENTRIES}}) {
	$count += $_->count_dirs;
    }
    return $count;
}

sub CvsDir::count_files {
    my ($self) = @_;
    my $count = 0;
    for (values %{$self->{ENTRIES}}) {
	$count += $_->count_files;
    }
    return $count;
}

sub CvsDir::count_filerevs {
    my ($self) = @_;
    my $count = 0;
    for (values %{$self->{ENTRIES}}) {
	$count += $_->count_filerevs;
    }
    return $count;
}

sub CvsDir::has_entry {
    my ($self, $entryname) = @_;
    $self->check;
    return $self->{ENTRIES}->{$entryname};
}

sub CvsDir::filerevs {
    my ($self) = @_;
    my @revs = ();
    for (values %{$self->{ENTRIES}}) {
	push @revs, $_->filerevs;
    }
    return @revs;
}

sub CvsDir::add_entry {
    my ($self, $entry) = @_;
    croak "$entry is not a CvsDirEntry" unless $entry->isa('CvsDirEntry');
    my $entryname = $entry->name;
    croak "duplicate entry $entryname"
	if exists $self->{ENTRIES}->{$entryname};
    $self->{ENTRIES}->{$entryname} = $entry;
    $self->check;
}

sub CvsDir::self_test {
    my $d0 = CvsDir->new(NAME => 'src');
    my $d1 = CvsDir->new(NAME => 'lib');

    my $a_file = CvsFile->new(NAME => 'a_file');
    my $a_rev = CvsFileRev->new(
				REV    => (bless { }, 'CvsRevID'),
				FILE   => $a_file,
				AUTHOR => (bless { }, 'CvsAuthor'),
				TIME   => (bless { }, 'CvsTime'),
				LOG    => (bless { }, 'CvsLog'),
				);
    $a_file->add_filerev($a_rev);

    $d0->add_entry($a_file);
    $d0->add_entry($d1);
    die unless keys %{$d0->{ENTRIES}} == 2;

    die unless $d0->has_entry('a_file') == $a_file;
    die if $d0->has_entry('another_file');

    die unless $d0->count_dirs == 2;
    die unless $d0->count_files == 1;
    die unless $d0->count_filerevs == 1;

    die unless $d1->count_dirs == 1;
    die unless $d1->count_files == 0;
    die unless $d1->count_filerevs == 0;

    die unless $d0->filerevs == 1;
    die unless ($d0->filerevs)[0] == $a_rev;

    die if (eval { $d0->add_entry(bless { }, 'Foo') })
	or $@ !~ /Foo=HASH\(.*\) is not a CvsDirEntry/;
    die unless keys %{$d0->{ENTRIES}} == 2;

    die if (eval { $d0->add_entry(CvsFile->new(NAME => 'a_file')) })
	or $@ !~ /duplicate entry a_file/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsRepository;
use Carp;

@CvsRepository::ISA = ('CheckedClass');

sub CvsRepository::type_info {
    return {
	required_args => {
	    PATH        => 'SCALAR',
	},
	members => {
	    ROOT        => 'CvsDir',
	    ALL_AUTHORS => 'HASH',
	    ALL_TAGS    => 'HASH',
	    ALL_LOGS    => 'HASH',
	},
    };
}

sub CvsRepository::count_dirs {
    my ($self) = @_;
    return $self->{ROOT}->count_dirs;
}

sub CvsRepository::count_files {
    my ($self) = @_;
    return $self->{ROOT}->count_files;
}

sub CvsRepository::count_filerevs {
    my ($self) = @_;
    return $self->{ROOT}->count_filerevs;
}

sub CvsRepository::count_authors {
    my ($self) = @_;
    return 0 + keys %{$self->{ALL_AUTHORS}};
}

sub CvsRepository::count_tags {
    my ($self) = @_;
    return 0 + keys %{$self->{ALL_TAGS}};
}

sub CvsRepository::count_logs {
    my ($self) = @_;
    return 0 + keys %{$self->{ALL_LOGS}};
}

sub CvsRepository::filerevs {
    my ($self) = @_;
    return $self->{ROOT}->filerevs;
}

sub CvsRepository::set_root {
    my ($self, $root) = @_;
    $self->{ROOT} = $root;
    $self->check;
}

sub CvsRepository::find_author($) {
    my ($self, $author_name) = @_;
    my $author = \$self->{ALL_AUTHORS}->{$author_name};
    $$author = CvsAuthor->new(NAME => $author_name) unless $$author;
    $self->check;
    return $$author;
}

sub CvsRepository::find_tag($) {
    my ($self, $tag_name) = @_;
    my $tag = \$self->{ALL_TAGS}->{$tag_name};
    $$tag = CvsTag->new(NAME => $tag_name) unless $$tag;
    $self->check;
    return $$tag;
}

sub CvsRepository::find_log($) {
    my ($self, $log_name) = @_;
    my $log = \$self->{ALL_LOGS}->{$log_name};
    $$log = CvsLog->new(NAME => $log_name) unless $$log;
    $self->check;
    return $$log;
}

sub CvsRepository::self_test {
    my $rep0 = CvsRepository->new(PATH => "/tmp/cvs");
    my $d0 = CvsDir->new(NAME => 'src');
    my $f0 = CvsFile->new(NAME => 'hello.c');
    my $r0 = CvsFileRev->new(
			     REV    => (bless { }, 'CvsRevID'),
			     FILE   => $f0,
			     AUTHOR => $rep0->find_author('jrandom'),
			     TIME   => (bless { }, 'CvsTime'),
			     LOG    => $rep0->find_log('I checked it in.'),
			     );
    $f0->add_filerev($r0);
    $d0->add_entry($f0);
    $rep0->set_root($d0);

    die unless $rep0->count_dirs == 1;
    die unless $rep0->count_files == 1;
    die unless $rep0->count_filerevs == 1;

    # test find_author

    my $author0 = $rep0->find_author('jrandom');
    die unless $author0->isa('CvsAuthor');
    die unless $author0->name eq 'jrandom';

    my $author1 = $rep0->find_author('jrandom');
    die unless $author1 == $author0;
    my $author2 = $rep0->find_author('kfred');
    die if $author2 == $author0;
    die unless $author2->name eq 'kfred';

    die unless $rep0->count_authors == 2;

    # test find_tag

    my $tag0 = $rep0->find_tag('alpha3.1r6');
    die unless $tag0->isa('CvsTag');
    die unless $tag0->name eq 'alpha3.1r6';

    my $tag1 = $rep0->find_tag('alpha3.1r6');
    die unless $tag1 == $tag0;
    my $tag2 = $rep0->find_tag('Beta2.4');
    die if $tag2 == $tag0;
    die unless $tag2->name eq 'Beta2.4';

    die unless $rep0->count_tags == 2;

    # test find_log

    my $log0 = $rep0->find_log('I checked it in.');
    die unless $log0->isa('CvsLog');
    die unless $log0->name eq 'I checked it in.';

    my $log1 = $rep0->find_log('I checked it in.');
    die unless $log1 == $log0;
    my $log2 = $rep0->find_log('It is fixed.');
    die if $log2 == $log0;
    die unless $log2->name eq 'It is fixed.';

    die unless $rep0->count_logs == 2;

    # test filerevs

    die unless $rep0->filerevs == 1;
    die unless ($rep0->filerevs)[0] == $r0;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsFileReader;
use Carp;
use Rcs;			# XXX this is from CPAN - include it

@CvsFileReader::ISA = ('CheckedClass');

sub CvsFileReader::type_info {
    return {
	required_args => {
	    PATH       => 'SCALAR',
	    REPOSITORY => 'CvsRepository',
	},
    };
}

sub CvsFileReader::read {
    my ($self) = @_;
    my $path = $self->{PATH};
    my $cvs_repo = $self->{REPOSITORY};
    my ($dir, $file) = split /\/(?=[^\/]*$ )/x, $path;
    $file =~ s/,v$//;
    
    my $rcs = Rcs->new;
    $rcs->rcsdir($dir);
    $rcs->file($file);
    my %rcs_dates = $rcs->dates;
    my %rcs_comments = $rcs->comments;

    # create CvsFile.

    my $cvs_file = CvsFile->new(NAME => $file);

    # Create all revs and add to CvsFile.

    for my $rcs_rev ($rcs->revisions) {
	my $cvs_author = $cvs_repo->find_author($rcs->author($rcs_rev));
	my $cvs_revid = CvsRevID->new(STRING => $rcs_rev);
	my $cvs_time = CvsTime->new(TIME_T => $rcs_dates{$rcs_rev});
	my $comment = $rcs_comments{$rcs_rev};
	$comment = '' unless defined $comment;
	my $cvs_log = $cvs_repo->find_log($comment);
	my $cvs_state = $rcs->state($rcs_rev);
	# print "$file $rcs_rev\n";
	my $cvs_file_rev = CvsFileRev->new(
					   REV    => $cvs_revid,
					   FILE   => $cvs_file,
					   AUTHOR => $cvs_author,
					   TIME   => $cvs_time,
					   LOG    => $cvs_log,
					   STATE  => $cvs_state,
					   );
	$cvs_author->add_rev($cvs_file_rev);
	$cvs_log->add_rev($cvs_file_rev);
	for ($rcs->symbol($rcs_rev)) {
	    next unless $_ ne '';
	    my $tag = $cvs_repo->find_tag($_);
	    $cvs_file_rev->add_tag($tag);
	    $tag->add_rev($cvs_file_rev);
	}
	$cvs_file->add_filerev($cvs_file_rev);
    }

    $self->check;
    return $cvs_file;
}

sub CvsFileReader::self_test {
    # Can't selftest this module - will use functional test.
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsDirReader;		# Read a local CVS directory
use Carp;

@CvsDirReader::ISA = ('CheckedClass');

sub CvsDirReader::type_info {
    return {
	required_args => {
	    PATH       => 'SCALAR',
	    REPOSITORY => 'CvsRepository',
	},
	members => {
	    PARENT_DIR => 'CvsDir',
	}
    };
}

sub CvsDirReader::read {
    my ($self) = @_;
    my ($cvs_repo) = $self->{REPOSITORY};
    my ($dir_name) = $self->{PATH} =~ /([^\/]*)$/;
    my $cvs_dir = CvsDir->new(NAME => $dir_name);
    # print "reading $self->{PATH}...\n";
    $self->_read_path($cvs_dir, $cvs_repo, $self->{PATH}, 0);
    return $cvs_dir;
}

sub CvsDirReader::_read_path {
    my ($self, $cvs_dir, $cvs_repo, $dir_path, $in_attic) = @_;
    my $attic_path;
    local(*D);
    opendir D, "$dir_path" or do { warn "$dir_path: $!\n"; return; };
    my $entry;
    while ($entry = readdir(D)) {
        next if $entry =~ /^\.\.?$/;
	my $entry_path = "$dir_path/$entry";
	if (-d $entry_path) {

	    # This is a subdirectory.

	    next if $entry =~ /^CVS$/;
	    if ($entry =~ /^Attic$/) {

		# Append Attic entries to current dir.

		$attic_path = $entry_path;
	    } else {
		my $subdir_reader = CvsDirReader->new(
					PATH       => $entry_path,
					REPOSITORY => $cvs_repo,
						      );
		my $subdir = $subdir_reader->read;
		$cvs_dir->add_entry($subdir) if $subdir;
		$subdir->set_parent($cvs_dir);
	    }
	} elsif (-f $entry_path and $entry =~ /,v$/) {

	    # This is an RCS file.

	    # Skip if this is an attic file that matches a non-attic file.
	    next if $in_attic and $cvs_dir->has_entry(substr($entry, 0, -2));

	    my $file_reader = CvsFileReader->new(
						 PATH       => $entry_path,
						 REPOSITORY => $cvs_repo,
						 );
	    my $file = $file_reader->read;
	    $cvs_dir->add_entry($file) if $file;
	    $file->set_parent($cvs_dir);

	} else {
	    warn "$entry_path: ignored\n" unless $entry_path =~ m|/CVSROOT/|;
	}
    }
    closedir D;
    if ($attic_path) {
	$self->_read_path($cvs_dir, $cvs_repo, $attic_path, 1);
    }
}

sub CvsDirReader::self_test {
    # Can't selftest this module - will use functional test.
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsLocalRepositoryReader; # Read a local CVS repository
use Carp;

@CvsLocalRepositoryReader::ISA = ('CheckedClass');

sub CvsLocalRepositoryReader::type_info {
    return {
	required_args => {
	    PATH => 'SCALAR',
	},
    };
}

sub CvsLocalRepositoryReader::read {
    my ($self) = @_;
    my $repo = CvsRepository->new(PATH => $self->{PATH});
    my $root_reader = CvsDirReader->new(
					PATH => $self->{PATH},
					REPOSITORY => $repo
					);
    my $root = $root_reader->read;
    $repo->set_root($root);
    return $repo;
}

sub CvsLocalRepositoryReader::self_test {
    # Can't selftest this module - will use functional test.
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package Commit;
use Carp;

@Commit::ISA = ('CheckedClass');

sub Commit::type_info {
    return {
	required_args => {
	    DURATION => 'SCALAR',
	},
	members => {
	    REVS     => 'ARRAY',
	    EARLIEST => 'SCALAR',
	    LATEST   => 'SCALAR',
	},
    };
}

sub Commit::filerevs {
    my ($self) = @_;
    return $self->{REVS};
}

sub Commit::accepts_filerev {
    my ($self, $rev) = @_;
    croak "Commit::accepts_filerev: $rev is not a CvsFileRev"
	unless $rev->isa('CvsFileRev');
    return 1 unless $self->{REVS} and @{$self->{REVS}};
    my $rev0 = $self->{REVS}[0];
    my $dur = $self->{DURATION};
    my $earliest = $self->{EARLIEST};
    my $latest = $self->{LATEST};
    my $rev_file = $rev->get_file;

    # This is the heuristic that determines whether a filerev
    # can be merged into a commit.
    #
    # The rules are:
    #	Author must match.
    #	Log message must match (and be nonempty).
    #	Time must be within DURATION seconds of another rev in the commit.
    #	File must not already be in commit.

    return 0 unless $rev->get_author == $rev0->get_author;
    return 0 unless $rev->get_log->name =~ /\s*/;
    return 0 unless $rev->get_log == $rev0->get_log;
    return 0 if $rev->get_time->time_t < $earliest - $dur;
    return 0 if $rev->get_time->time_t > $latest + $dur;
    return 0 if grep { $_->get_file == $rev_file } @{$self->{REVS}};
    return 1;
}

sub Commit::add_filerev {
    my ($self, $rev) = @_;
    croak "Commit::add_filerev: $rev is not a CvsFileRev"
	unless $rev->isa('CvsFileRev');
    my $t = $rev->get_time->time_t;
    $self->{EARLIEST} = $t if !$self->{EARLIEST} or $t < $self->{EARLIEST};
    $self->{LATEST}   = $t if !$self->{LATEST} or $t > $self->{LATEST};
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub Commit::self_test {
    my $file0 = CvsFile->new(NAME => 'hello.c');
    my $file1 = CvsFile->new(NAME => 'Makefile');
    my $auth0 = CvsAuthor->new(NAME => 'jrandom');
    my $auth1 = CvsAuthor->new(NAME => 'kfred');
    my $log0 = CvsLog->new(NAME => 'I checked it in.');
    my $log1 = CvsLog->new(NAME => 'I fixed it.');
    my $rev0 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.1'),
			       FILE   => $file0,
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 1000000),
			       LOG    => $log0,
			       );

    my $c = Commit->new(DURATION => 5);
    die unless $c->accepts_filerev($rev0);
    $c->add_filerev($rev0);

    # Should succeed.

    my $rev1 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => $file1,
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 1000001),
			       LOG    => $log0,
			       );
    die unless $c->accepts_filerev($rev1);
    $c->add_filerev($rev1);

    # Should fail.  Different author.

    my $rev2 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => (bless { }, 'CvsFile'),
			       AUTHOR => $auth1,
			       TIME   => CvsTime->new(TIME_T => 1000001),
			       LOG    => $log0,
			       );
    die if $c->accepts_filerev($rev2);

    # Should fail.  Different log message.

    my $rev3 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => (bless { }, 'CvsFile'),
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 1000001),
			       LOG    => $log1,
			       );
    die if $c->accepts_filerev($rev3);

    # Should fail.  Too early.

    my $rev4 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => (bless { }, 'CvsFile'),
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 999990),
			       LOG    => $log0,
			       );
    die if $c->accepts_filerev($rev4);

    # Should fail.  Too late.

    my $rev5 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => (bless { }, 'CvsFile'),
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 999990),
			       LOG    => $log0,
			       );
    die if $c->accepts_filerev($rev5);

    # Should fail.  Same file.

    my $rev6 = CvsFileRev->new(
			       REV    => CvsRevID->new(STRING => '1.2'),
			       FILE   => $file1,
			       AUTHOR => $auth0,
			       TIME   => CvsTime->new(TIME_T => 1000000),
			       LOG    => $log0,
			       );
    die if $c->accepts_filerev($rev6);

    die if (eval { $c->accepts_filerev(bless { }, 'Foo') })
	or $@ !~ /Commit::accepts_filerev: Foo=HASH\(.*\) is not a CvsFileRev/;

    die if (eval { $c->add_filerev(bless { }, 'Foo') })
	or $@ !~ /Commit::add_filerev: Foo=HASH\(.*\) is not a CvsFileRev/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package SelfTest;

sub run {
    my $classes;
    $classes = sub {
	my ($space) = (@_, '::');
	my @r;
	for (eval "keys %${space}") {
	    next if /^::/;
	    push @r, substr "$space$_", 2, -2 if /::$/;
	    push @r, (&$classes("${space}$_"))
		if /::$/ and $_ ne $space and $_ !~ /^(main\:\:)+$/;
	}
	return @r;
    };
    for (&$classes) {
	if (eval "\$${_}::{self_test}") {
	    # print "self test $_\n";
	    unless (eval "$_->self_test; 1") {
		die "$@$_ self test FAILED\n";
	    }
	}
    }
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CheckedClass;		# XXX need a better class name
				# StrongTyped, Declared, StaticTyped?
use Carp;

=head1 NAME

CheckedClass - do static type checking

=head1 SYNOPSIS

    package YourClass;
    @ISA = ('CheckedClass');    # Your class isa CheckedClass.

    sub type_info {	     # Your class MUST define type_info.
	return {
	    required_args => {  # Caller MUST pass these to new().
		ARGNAME => 'YourArgType'
	    },
	    optional_args => {  # Caller MAY pass these to new().
		OTHER => 'SCALAR'
	    },
	    members => {	     # YourClass MAY create these.
		SOME_HASH => 'HASH'
	    }
	};
    }

    # Inherit CheckedClass::new, which calls YourClass::initialize.
    sub initialize {
	my ($self) = @_;
	$self->{SOME_HASH} = &whatever;
    }

    sub your_method {
	my ($self) = @_;
	$self->{SOME_HASH}->{some_key}++;
	$self->check;	    # May typecheck %{$self} at any time.
    }

    package main;
    $ya = bless {}, YourArgType;
    $yc1 = YourClass->new(ARGNAME => $ya);
    $yc2 = YourClass->new(ARGNAME => $ya, OTHER => 17);

=head1 DESCRIPTION

CheckedClass provides a way for a subclass to declare the types of its
member variables and constructor arguments and check those types at run
time.

A checked class must define the method B<type_info>, which must return
a hash with three entries: B<required_args>, B<optional_args>, and
B<members>.  Each of the three entries is a hash whose keys are member
variables and whose values describe the types of the member variables.

CheckedClass provides a constructor, B<new>, that enforces the rules
in type_info.  Callers of B<new> must provide all arguments listed in
required_args, as keyword arguments, with values of the type
specified.  Callers of B<new> may also provide keyword arguments
listed in optional_args, which are also type checked.  Any keywords
not listed or keywords with values of the wrong type will cause a
runtime error.  B<new> creates the object as a hash reference and
inserts correct arguments into the hash.  Then it calls the
B<initialize> method, which may be provided by the subclass.

CheckedClass also provides a B<check> member that may be called at
any time to verify that an object only has the permitted members and
that they have the permitted types.

CheckedClass supports multiple inheritance, but all of the ancestors
of a checked class must be derived from CheckedClass.

=cut

$CheckedClass::Skip_Checks = 0;
my %Type_Info_Cache;

sub CheckedClass::new {
    my $template = shift;
    my $classname = ref($template) || $template; # see perlobj(1) man page
    my $self = do {
 	local $SIG{__WARN__} = sub { };	# ignore "Odd number of elements" msg
	bless { @_ }, $classname;
    };
    $self->check_new();
    $self->initialize();
    $self->check();
    return $self;
    
}

sub CheckedClass::initialize {
    # placeholder, subclasses can override.
}

sub CheckedClass::check_new {
    return if $CheckedClass::Skip_Checks;
    my ($self) = @_;
    my $classname = ref($self);
    my $info = $self->merged_type_info();
    for (@{$info->{required_args}}) {
	croak "required arg \"$_\" missing in $classname constructor"
	    unless exists $self->{$_};
    }
    while (my ($arg, $val) = each %$self) {
	my $expected_type = $info->{arg_types}->{$arg};
	croak "unknown arg $arg in $classname constructor"
	    unless $expected_type;
	my $actual_type = ref($val);
	unless ($actual_type) {
	    $actual_type = \$val;     # e.g., SCALAR(0x123).
	    $actual_type =~ s/\(.*//; # change "SCALAR(0x123)" to "SCALAR".
	}
	croak "arg $_ type mismatch in $classname constructor, " .
	    "expected $expected_type, got $actual_type"
		unless $actual_type->isa($expected_type);
    }
}

sub CheckedClass::check {
    return if $CheckedClass::Skip_Checks;
    my ($self) = @_;
    my $classname = ref($self);
    my $info = $self->merged_type_info();
    while (my ($member, $val) = each %$self) {
	my $expected_type = $info->{member_types}->{$member};
	croak "unknown member $member in $self"
	    unless $expected_type;
	my $actual_type = ref($val);
	unless ($actual_type) {
	    $actual_type = \$val; # e.g., SCALAR(0x123).
	    $actual_type =~ s/\(.*//; # change "SCALAR(0x123)" to "SCALAR".
	}
	croak "member $_ type mismatch in $self, " .
	    "expected $expected_type, got $actual_type"
		unless $actual_type->isa($expected_type);
    }
}

sub CheckedClass::merged_type_info {
    my ($self) = @_;
    my $class = ref($self) || $self;
    my $cached = $Type_Info_Cache{$class};
    return $cached if $cached;

    return { } if $class eq 'CheckedClass' or ! $class->isa('CheckedClass');

    my (%required, %arg_types, %member_types);

    # Get this class's type info.

    my $ti = $self->type_info();
    while (my ($arg, $type) = each %{$ti->{required_args}}) {
	$required{$arg}++;
	$arg_types{$arg} = $type;
	$member_types{$arg} = $type;
    }
    while (my ($arg, $type) = each %{$ti->{optional_args}}) {
	croak "$class member $arg multiply defined"
	    if exists $member_types{$arg};
	$arg_types{$arg} = $type;
	$member_types{$arg} = $type;
    }
    while (my ($arg, $type) = each %{$ti->{members}}) {
	croak "$class member $arg multiply defined"
	    if exists $member_types{$arg};
	$member_types{$arg} = $type;
    }

    # Merge superclasses' type info.

    my @isa = (eval "\@${class}::ISA");
    for (@isa) {
	my $super_info = $_->merged_type_info();

	# required_args is the union of all classes' required args.

	for (@{$super_info->{required_args}}) {
	    $required{$_}++;
	}

	# arg_types' keys is the union of all classes' arg types.
	# The values are the intersection.

	while (my ($k, $v) = each %{$super_info->{arg_types}}) {
	    my $a = \$arg_types{$k};
	    if (!$$a) {
		$$a = $v;
	    } elsif (!$$a->isa($v)) {
		croak "${_} arg $k is incompatible with $_ arg $k"
		    unless $v->isa($$a);
		$$a = $v;
	    }
	}

	# member_types is the same as arg_types.

	while (my ($k, $v) = each %{$super_info->{member_types}}) {
	    my $a = \$member_types{$k};
	    if (!$$a) {
		$$a = $v;
	    } elsif (!$$a->isa($v)) {
		croak "${_}->{$k} is incompatible with $_->{$k}"
		    unless $v->isa($$a);
		$$a = $v;
	    }
	}
    }
    my $list = {
	required_args => [ keys %required ],
	arg_types     => { %arg_types },
	member_types  => { %member_types },
    };
    $Type_Info_Cache{$class} = $list;
    return $list;
}

# XXX self-test CheckedClass.

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package main;

sub usage {
    die "use: $0 [-d cvs-repository-spec] new-subversion-root-dir\n";
}

sub is_local {			# Is this a local cvs root?
    my ($cvsroot) = @_;
    return $cvsroot =~ /^[^:]/;
}

sub get_vm_size {		# This only works on Linux.
    my $size;
    local *M;
    open(M, "</proc/$$/status") or return 0;
    while (<M>) {
	do { $size = $1; last; } if /^VmSize\:\s*(\d+) kB/;
    }
    close M;
    return $size;
}
sub print_stats {
    my ($repo) = @_;
    my $dir_count = $repo->count_dirs;
    my $file_count = $repo->count_files;
    my $rev_count = $repo->count_filerevs;
    my $author_count = $repo->count_authors;
    my $tag_count = $repo->count_tags;
    my $log_count = $repo->count_logs;

    print "$dir_count directories, ";
    print "$file_count files, ";
    print "$rev_count file revisions\n";
    print "$author_count unique authors, ";
    print "$tag_count unique tags, ";
    print "$log_count unique log messages\n";
}

sub build_commit_list {
    my ($cvs_repo, $commit_duration) = @_;
    my @revs = sort {
	$a->get_time->compare_to($b->get_time)
    } $cvs_repo->filerevs;

    my @commit_list;
    my $commit;
    for my $rev (@revs) {
	unless ($commit and $commit->accepts_filerev($rev)) {
	    $commit = Commit->new(DURATION => $commit_duration);
	    push @commit_list, $commit;
	} 
	$commit->add_filerev($rev);
    }
    return @commit_list;
}

# The main program begins here.

$0 =~ s|.*/||;
$| = 1;

my $cvsroot = $ENV{CVSROOT};
if (@ARGV > 1 and $ARGV[0] eq '-d') {
    shift;
    $cvsroot = shift;
}
my $svnroot = $ARGV[0];

&usage(1)
    unless $svnroot;

if(!$cvsroot) {
    if( -r "CVS/Root" ) {
        if(open(ROOT, "<CVS/Root")) {
            $cvsroot=<ROOT>;
            chomp $cvsroot;
            close(ROOT);
        }
    }
    if(!$cvsroot) {
        die "No -d and no CVS directory found";
    }
}


&SelfTest::run;

# XXX Need to handle the various cvs connection methods better.

system("date");
my $mem_before = &get_vm_size;
my $cvs_repo;
if (&is_local($cvsroot)) {
    my $repo_reader = CvsLocalRepositoryReader->new(PATH => $cvsroot);
    $cvs_repo = $repo_reader->read;
} else {
    die "$0: remote CVS repository not implemented\n";
}
&print_stats($cvs_repo);

my @commits = &build_commit_list($cvs_repo, 3);

for (@commits) {
    print "commit\n";
    for (@{$_->{REVS}}) {
	my $path = $_->get_file->path;
	$path =~ s|^[^/]*/||;
	print "\t$path $_->{REV}->{STRING}\n";
    }
    print "\n";
}

my $mem_after = &get_vm_size;

print "used ", $mem_after - $mem_before, " Kbytes\n";
system("date");
