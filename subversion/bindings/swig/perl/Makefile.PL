#!/usr/bin/perl
use ExtUtils::MakeMaker;
use Cwd 'abs_path';
use Config;

my $perl_path = $Config{perlpath};
if ($^O ne 'VMS') {
  $perl_path .= $Config{_exe} unless $perl_path =~ m/$Config{_exe}$/i;
}

my $swig_version;
open TOPMAKE, "<../../../../Makefile";
while (<TOPMAKE>) {
  /^SWIG_VERSION\s*=\s*(\d+)\s*$/;
  if (defined($1)) {
    $swig_version = $1;
  }
}
close TOPMAKE;

my @modules = qw/client delta fs ra repos wc/;
my @ldpaths = ('../.libs',
               map {"../../../libsvn_$_/.libs"} (@modules, qw/diff subr
                                                              ra_local
                                                              ra_svn
                                                              ra_dav/));
my @ldmodules = map {"-lsvn_$_-1"} (@modules, qw/diff subr/);

my $apr_config = $ENV{APR_CONFIG} || '/usr/local/lib/apache2/apr-config';
$apr_config = 'apr-config' unless -e $apr_config;
my $apu_config = $ENV{APU_CONFIG} || '/usr/local/lib/apache2/apu-config';
$apu_config = 'apu-config' unless -e $apu_config;

my $apr_shlib_path_var = `$apr_config --shlib-path-var`;
my $apr_cflags = `$apr_config --includes`;
my $apu_cflags = `$apu_config --includes`;
my $apr_ldflags = `$apr_config --link-ld --libs`
   if $^O eq 'darwin';

chomp $apr_shlib_path_var;
chomp $apr_cflags;
chomp $apu_cflags;
chomp $apr_ldflags;

my %config = (
    ABSTRACT => 'Perl bindings for Subversion',
    CCFLAGS  => join(' ', $apr_cflags, $apu_cflags, 
                     `$perl_path -MExtUtils::Embed -e ccopts`,
                     ' -I.. -I../../../include -g'),
    OBJECT => q/$(O_FILES)/,
    LIBS => [join(' ', $apr_ldflags,
                  (map {$_ = abs_path($_); "-L$_"} @ldpaths),
                  @ldmodules, '-lsvn_swig_perl-1',
                  `swig -perl -ldflags`)],
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
	      clean => { FILES => "*.hi *.c *.bs".
                         join(' Makefile.','',@modules) }
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
   
   my $module_c_files = join (' ',map { "svn_$_.c"} @modules);
   my $module_make_commands = join ('',map {"\tmake -f Makefile.$_\n"} @modules);

   my $flags;
   if ($swig_version >= 103020) {
     $flags = '-noruntime -noproxy';
   } else {
     $flags = '-c';
   }
   
   my $swig_command = "swig $flags -nopm -perl " .
                      "-I.. -I../../../include $apr_cflags";

   my $swig_modules_command = join ('',
                                    map {"\nsvn_$_.c : ../svn_$_.i ".
                                         "ra_plugin.hi ra_reporter.hi ".
                                         "delta_editor.hi\n" .
                                         "\t$swig_command -module SVN::_".
                                         main::perlish($_) .
                                         " -o svn_$_.c ../svn_$_.i\n"}
                                         @modules
                                   );

  my $fullperlrun = "$apr_shlib_path_var=" . join(':',@ldpaths);
  
   return <<"EOPOST";
all :: modules
test :: modules
install :: modules

par :: all
\t$perl_path -MPAR::Dist -e"blip_to_par(name=>'SVN",version=>'`$perl_path -Mblib -MSVN::Core -e 'print $SVN::Core::VERSION'`')"

ra_plugin.hi: ../../../include/svn_ra.h
\t$perl_path h2i.pl ../../../include/svn_ra.h svn_ra_plugin_t > \$@

ra_reporter.hi: ../../../include/svn_ra.h
\t$perl_path h2i.pl ../../../include/svn_ra.h svn_ra_reporter_t > \$@

delta_editor.hi: ../../../include/svn_delta.h
\t$perl_path h2i.pl ../../../include/svn_delta.h svn_delta_editor_t > \$@

modules :: $module_c_files
$module_make_commands\t\$(NOECHO) \$(TOUCH) \$\@

core.c :: ../core.i
\t$swig_command -module SVN::_Core -o core.c ../core.i

$swig_modules_command

FULLPERLRUN=$fullperlrun \$(FULLPERL)

EOPOST

}
