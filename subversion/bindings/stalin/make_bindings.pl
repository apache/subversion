#!/usr/bin/perl

use File::Slurp qw /slurp/;
use Switch;
#Auto-generates stalin scheme bindings
#This is not particularly great code
#One possible replacement is with an emacs lisp program 
#and use cedet's sementatic or similar



#Main loop
print "Starting to generate bindings to $ARGV[0]..\n";
#open the list of header files to be processed
open (OUT , ">$ARGV[0]") or die "can not open output file for bindings";
while ($_ = <STDIN>) {
  chomp ($_);
  #print "handling $_\n";
  $text = process($_);
  if ($_ =~ /\/(.*)$/) { 
    print OUT ";$1\n";
  }
  print OUT "$text\n";
}
close (OUT);
print "finished\n";

sub process {
  my $file = shift @_;
  $text = "";
  `rm tags`;
  `ctags --sort=0 --format=2 --fields=+KlSn--af  $_ `;
  $fulltext = slurp($file);
  open (TIN , "tags") or warn "no tags!\n";
  while (<TIN>) {
    #If it is a function
    if ($_ =~ /function\t/)  { #Is it a function?
      @ctagparms = split (/\t/, $_);
      $name = $ctagparms[0];
      $pt = $ctagparms[2];
      #Attempt to lookup the function in the src file
      if ($fulltext =~ /\n$name\((.*?)\)/s) {
	$pt = $1;
      } elsif ($fulltext =~ /\*\s*$name\((.*?)\)/s) {
	$pt = $1;
      }
      #Assume we return a void*
      $rt = "VOID*";
      if ($fulltext =~ /int\s*\n\s*$name\(/si) {
	$rt = "INT";
      } elsif ($fulltext =~ /char\s*\n\s*$name\(/si) {
        $rt = "CHAR";
      } elsif ($fulltext =~ /char\*\s*\n\s*$name\(/si) {
        $rt = "CHAR*";
      }
      $takes = givetakes($pt);
      #Must have stalin_bindings_ since there is a function called list
      #re-defining list makes stalin scheme VERY angry ;)
      $text = "$text \n (define stalin_bindings_$name (foreign-procedure ( $takes ) $rt \"$name\"))";
    }
    
  }
  print $ft;
  close (TIN);
  #print "$text\n";
  #`rm tags`;#Clean up the tags file
  return $text;
}

#Return stalinized types
sub givetakes {
  my $in = shift @_;
  #Use r to accumalate return types
  my $r = "";
  if ($in =~ /\((.*)\)/) {
    $in = $1;
  } elsif ($in =~ /\((.*)\$/) {
    $in = $1;
  }
    #Split it on , to get out the different paramaters
    my @ct = split (/\,/, $in);
    foreach $myct (@ct) {
      #So we have a few different supported possibilities
      #See stalin README file
      switch ($myct) {
	case /int/i {$s = "INT";}
	
	case /signed char/i {$s = "SIGNED-CHAR";}
	
	case /unsigned char/i {$s = "UNSIGNED-CHAR";}

	case /char\s*\*/ {$s = "CHAR*";}

	case /char/i {$s = "CHAR";}
	
	case /short/i {$s = "SHORT";}
		
	case /long double/i {$s = "LONG-DOUBLE";}
	
	case /long/i {$s = "LONG";}
	
	case /float/i {$s = "FLOAT";}
			
	  #Explicitly don't handle void*
	  else {$s = "VOID*";}
      }
      #Add the type to the end of the list
      $r = "$r $s";
    }
  return $r;
}
