#!/usr/bin/perl
use ExtUtils::MakeMaker;

my @modules = qw/client delta fs ra repos wc/;
my @ldmodules = map {"-lsvn_$_-1"} (@modules, qw/subr/);

my $apr_config = $ENV{APR_CONFIG} || '/usr/local/lib/apache2/apr-config';
$apr_config = 'apr-config' unless -e $apr_config;

my $apr_cflags = `$apr_config --includes`;
my $apr_ldflags = `$apr_config --cflags --libs`;

chomp $apr_cflags;
chomp $apr_ldflags;

my %config = (
    ABSTRACT => 'Perl bindings for Subversion',
    CCFLAGS  => join(' ', $apr_cflags, `perl -MExtUtils::Embed -e ccopts`,
		     ' -I.. -I../../../include -g'),
    OBJECT => q/$(O_FILES)/,
    dynamic_lib => {
	OTHERLDFLAGS => join(' ', $apr_ldflags, '-L/usr/local/lib',
			     @ldmodules, '-lsvn_swig_perl-1',
			     `swig -perl -ldflags`),
    },
);

sub perlish {
    local $_ = $_[0];
    s/^(\w)/\U$1/;
    $_;
}

WriteMakefile(%config, NAME => 'SVN::_Core', C => ['core.c'],
	      PM => {map { ("$_.pm" => "\$(INST_LIBDIR)/$_.pm") }
		     map { perlish $_ }
		     ('base', 'core', @modules)},
	      clean => { FILES => "*.hi Makefile.[a-z]* *.c *.bs" }
	     );

for (@modules) {
    WriteMakefile(%config,
		  MAKEFILE=> "Makefile.$_",
		  NAME    => "SVN::_".perlish($_),
		  C	=> ["svn_$_.c"],
		 );
}

# the dependencies need to be fixed

sub MY::postamble {
   package MY ;
   return join('', "all :: modules\ntest :: modules\ninstall :: modules\n\n",
	       "par :: all\n\t",
	       q{perl -MPAR::Dist -e"blib_to_par(name=>'SVN',version=>'`perl -Mblib -MSVN::Core -e'print $$SVN::Core::VERSION'`')"},"\n\n",
	       # h2i
	       "ra_plugin.hi: ../../../include/svn_ra.h\n\tperl h2i.pl ../../../include/svn_ra.h svn_ra_plugin_t > \$@\n\n",
	       "ra_reporter.hi: ../../../include/svn_ra.h\n\tperl h2i.pl ../../../include/svn_ra.h svn_ra_reporter_t > \$@\n\n",
	       "delta_editor.hi: ../../../include/svn_delta.h\n\tperl h2i.pl ../../../include/svn_delta.h svn_delta_editor_t > \$@\n\n",
	       "modules :: ",(map { " svn_$_.c"} @modules),"\n",
	       (map {"\tmake -f Makefile.$_\n"} @modules),
	       "\t\$(NOECHO) \$(TOUCH) \$\@",
	       # swig
	       "\ncore.c :: ../core.i\n",
	       "\tswig -c -nopm -perl -I.. -I../../../include $apr_cflags -module SVN::_Core -o core.c ../core.i\n", 
	       (map {"\nsvn_$_.c : ../svn_$_.i ra_plugin.hi ra_reporter.hi delta_editor.hi\n".
			"\tswig -c -nopm -perl -I.. -I../../../include $apr_cflags -module SVN::_".main::perlish($_)." -o svn_$_.c ../svn_$_.i\n"}
	       @modules),

	      );
}
