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

1;
