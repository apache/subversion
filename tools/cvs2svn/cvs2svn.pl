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

sub CvsTime::compare_to {
    my ($self, $peer) = @_;
    $self->check;
    croak "CvsTime can't compare to $peer" unless $peer->isa('CvsTime');
    return $self->{TIME_T} <=> $peer->{TIME_T};
}

sub CvsTime::self_test {
    my $time0 = new CvsTime(STRING => '1998.10.31.12.15.54');
    my $time1 = $time0->new(STRING => '98.10.31.12.16.54');

    die if (eval { new CvsTime(STRING => '1998.10.31.12.15') })
	or $@ !~ /^malformed/;
    die if (eval { new CvsTime(STRING => '1998.10.31.12.15.54.32') })
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
    my $id0 = new CvsRevID(STRING => '1.4.2.3');

    die if (eval { new CvsRevID(STRING => '1..2') })
	or $@ !~ /^malformed/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsAuthor;
use Carp;

@CvsAuthor::ISA = ('CheckedClass');

sub CvsAuthor::type_info {
    return {
	required_args => {
	    NAME => 'SCALAR',
	},
	members => {
	    REVS => 'ARRAY',
	},
    };
}

sub CvsAuthor::name {
    my ($self) = @_;
    return $self->{NAME};
}

sub CvsAuthor::checked_in_rev {
    my ($self, $rev) = @_;
    croak "A CvsAuthor can't check in $rev" unless $rev->isa('CvsFileRev');
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub CvsAuthor::self_test {
    my $auth0 = new CvsAuthor(NAME => 'jrandom');

    die unless $auth0->name eq 'jrandom';

    $auth0->checked_in_rev(bless { }, 'CvsFileRev');

    die if (eval { $auth0->checked_in_rev(bless { }, 'Foo') } )
	or $@ !~ /^A CvsAuthor can\'t check in Foo/;
}

#  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #  #
package CvsTag;
use Carp;

@CvsTag::ISA = ('CheckedClass');

sub CvsTag::type_info {
    return {
	required_args => {
	    NAME => 'SCALAR',
	},
	members => {
	    REVS => 'ARRAY',
	},
    };
}

sub CvsTag::name {
    my ($self) = @_;
    return $self->{NAME};
}

sub CvsTag::tagged_rev {
    my ($self, $rev) = @_;
    croak "A CvsTag can't tag a $rev" unless $rev->isa('CvsFileRev');
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub CvsTag::self_test {
    my $tag0 = new CvsTag(NAME => 'beta2.3');

    $tag0->tagged_rev(bless { }, 'CvsFileRev');
    $tag0->tagged_rev(bless { }, 'CvsFileRev');
    die unless @{$tag0->{REVS}} == 2;

    die if (eval { $tag0->tagged_rev(bless { }, 'Foo') })
	or $@ !~ /^A CvsTag can\'t tag a Foo/;
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
	},
	optional_args => {
	    PREDECESSOR => 'CvsFileRev', # undefined for initial rev.
	    TAGS        => 'ARRAY',
	    LOG         => 'SCALAR',
	    STATE       => 'SCALAR',
	},
    };
}

sub CvsFileRev::add_tag {
    my ($self, $tag) = @_;
    croak "$tag is not a CvsTag" unless $tag->isa('CvsTag');
    push @{$self->{TAGS}}, $tag;
    $self->check;
}

sub CvsFileRev::self_test {
    my $fr0 = new CvsFileRev(
			     REV    => (bless { }, 'CvsRevID'),
			     FILE   => (bless { }, 'CvsFile'),
			     AUTHOR => (bless { }, 'CvsAuthor'),
			     TIME   => (bless { }, 'CvsTime'),
			     LOG    => 'I checked it in.',
			     );
    $fr0->add_tag(bless { }, 'CvsTag');

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

sub CvsDirEntry::self_test {
    my $de0 = CvsDirEntry->new(NAME => 'slug');
    die unless $de0->name eq 'slug';
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

sub CvsFile::count_revs {
    my ($self) = @_;
    return 0 + @{$self->{REVS}};
}

sub CvsFile::add_filerev {
    my ($self, $rev) = @_;
    croak "$rev is not a CvsFileRev" unless $rev->isa('CvsFileRev');
    push @{$self->{REVS}}, $rev;
    $self->check;
}

sub CvsFile::self_test {
    my $f0 = CvsFile->new(
			  NAME => 'hello.c'
			  );
    $f0->add_filerev(bless { }, 'CvsFileRev');
    $f0->add_filerev(bless { }, 'CvsFileRev');
    die unless @{$f0->{REVS}} == 2;

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

sub CvsDir::count_revs {
    my ($self) = @_;
    my $count = 0;
    for (values %{$self->{ENTRIES}}) {
	$count += $_->count_revs;
    }
    return $count;
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
    my $d0 = new CvsDir(NAME => 'src');

    $d0->add_entry(CvsFile->new(NAME => 'a_file'));
    $d0->add_entry(CvsDir->new(NAME => 'a_dir'));
    die unless keys %{$d0->{ENTRIES}} == 2;

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
	    PATH             => 'SCALAR',
	},
	members => {
	    ROOT             => 'CvsDir',
	    ALL_AUTHORS      => 'HASH',
	    ALL_TAGS         => 'HASH',
	    ALL_REVS_BY_TIME => 'ARRAY',
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

sub CvsRepository::count_revs {
    my ($self) = @_;
    return $self->{ROOT}->count_revs;
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

sub CvsRepository::self_test {
    my $rep0 = CvsRepository->new(PATH => "/tmp/cvs");

    # test find_author

    my $author0 = $rep0->find_author('jrandom');
    die unless $author0->isa('CvsAuthor');
    die unless $author0->name eq 'jrandom';

    my $author1 = $rep0->find_author('jrandom');
    die unless $author1 == $author0;
    my $author2 = $rep0->find_author('kfred');
    die if $author2 == $author0;
    die unless $author2->name eq 'kfred';

    # test find_tag

    my $tag0 = $rep0->find_tag('alpha3.1r6');
    die unless $tag0->isa('CvsTag');
    die unless $tag0->name eq 'alpha3.1r6';

    my $tag1 = $rep0->find_tag('alpha3.1r6');
    die unless $tag1 == $tag0;
    my $tag2 = $rep0->find_tag('Beta2.4');
    die if $tag2 == $tag0;
    die unless $tag2->name eq 'Beta2.4';
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
	my $cvs_state = $rcs->state($rcs_rev);
	my $cvs_file_rev = CvsFileRev->new(
					   REV    => $cvs_revid,
					   FILE   => $cvs_file,
					   AUTHOR => $cvs_author,
					   TIME   => $cvs_time,
					   LOG    => $rcs_comments{$rcs_rev},
					   STATE  => $cvs_state,
					   );
	for ($rcs->symbol($rcs_rev)) {
	    next unless $_ ne '';
	    my $tag = $cvs_repo->find_tag($_);
	    $cvs_file_rev->add_tag($tag);
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
    };
}

sub CvsDirReader::read {
    my ($self) = @_;
    my ($cvs_repo) = $self->{REPOSITORY};
    my ($dir_name) = $self->{PATH} =~ /([^\/]*)$/;
    my $cvs_dir = CvsDir->new(NAME => $dir_name);
    $self->_read_path($cvs_dir, $cvs_repo, $self->{PATH});
    return $cvs_dir;
}

sub CvsDirReader::_read_path {
    my ($self, $cvs_dir, $cvs_repo, $dir_path) = @_;
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

		$self->_read_path($cvs_dir, $cvs_repo, $entry_path);
	    } else {
		my $subdir_reader = CvsDirReader->new(
					PATH       => $entry_path,
					REPOSITORY => $cvs_repo,
						      );
		my $subdir = $subdir_reader->read;
		$cvs_dir->add_entry($subdir) if $subdir;
	    }
	} elsif (-f $entry_path and $entry =~ /,v$/) {

	    # This is an RCS file.

	    confess "cvs_repo" unless ref($cvs_repo) eq 'CvsRepository';
	    my $file_reader = CvsFileReader->new(
						 PATH       => $entry_path,
						 REPOSITORY => $cvs_repo,
						 );
	    my $file = $file_reader->read;
	    $cvs_dir->add_entry($file) if $file;

	} else {
	    warn "$entry_path: ignored\n" unless $entry_path =~ m|/CVSROOT/|;
	}
    }
    closedir D;
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
	    unless defined $self->{$_};
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
	$arg_types{$arg} = $type;
	$member_types{$arg} = $type;
    }
    while (my ($arg, $type) = each %{$ti->{members}}) {
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

sub print_stats {
    my ($repo) = @_;
    my $dir_count = $repo->count_dirs;
    my $file_count = $repo->count_files;
    my $rev_count = $repo->count_revs;
    print "$dir_count directories, $file_count files, $rev_count revisions\n";
}

# The main program begins here.

$0 =~ s|.*/||;

my $cvsroot = $ENV{CVSROOT};
if (@ARGV > 1 and $ARGV[0] eq '-d') {
    shift;
    $cvsroot = shift;
}
my $svnroot = $ARGV[0];

&usage(1)
    unless $svnroot;

die "$0: No CVSROOT specified.  Please use the `-d' option.\n"
    unless $cvsroot;

&SelfTest::run;

# XXX Need to handle the various cvs connection methods better.

my $cvs_repo;
if (&is_local($cvsroot)) {
    my $repo_reader = CvsLocalRepositoryReader->new(PATH => $cvsroot);
    $cvs_repo = $repo_reader->read;
} else {
    die "$0: remote CVS repository not implemented\n";
}

&print_stats($cvs_repo);
