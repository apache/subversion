#!/usr/bin/perl -w

### Convert Emacs outline mode documents to HTML.
### 
### Usage: "htmlize.pl [FILENAME.txt]" to produce FILENAME.html
###     (FILENAME defaults to stdin.)

use strict;

############ Customizable globals ############

my $list_type = "ul";     # HTML unordered list
my $dest_ext = ".html";

############ End Customizable globals ############


my $source = shift || "-";

my ($base) = split (/\./, $source);
my $dest;
if ((! defined ($base)) or ($base eq "-")) {
  $dest = "-"; # default to stdout
}
else {
  $dest = "${base}${dest_ext}";  # otherwise use <FILE>.html
}

my $star_level = 0;          # outline heading level
my $list_level = 0;          # depth in html lists (related to star_level)
my $seen_first_heading = 0;  # Becomes 1 and stays 1 after first heading
my $inside_pre = 0;          # set to 1 when inside <pre>...</pre> tags

open (SOURCE, "$source") or die ("trouble reading $source ($!)");
open (DEST, ">$dest") or die ("trouble writing $dest ($!)");

# Start off an HTML page with a white background:
print DEST "<html>\n";
print DEST "<body bgcolor=\"#FFFFFF\" fgcolor=\"#000000\">\n";

# Put the outline document into it, htmlifying as we go:
while (<SOURCE>)
{
  my $quoted_mail_line = 0;
  my $num_stars = &count_stars ($_);

  next if (/please be in -\*- outline -\*- mode/);

  if (/^>/) {
    $quoted_mail_line = 1;
  }

  chomp;

  if ($num_stars) {
    # Strip leading stars from heading lines
    $_ =~ s/^\*+ //;
    # Indicate that we're no longer in the preamble
    $seen_first_heading = 1;
  }

  $_ = &escape_html ($_);
  $_ = &expand_urls ($_);
  $_ = &interpret_asterisks_in_context ($_);

  if ($num_stars == 1)
  {
    $star_level = 1;

    if ($list_level > $num_stars) {
      for (my $l = $list_level; $l > 0; $l--) {
        print DEST "\n</$list_type>\n\n";
        $list_level--;
      }
    }

    print DEST "\n\n<p>\n<hr>\n<p>\n\n";
    print DEST "<center>\n<h1>$_</h1>\n</center>\n";
  }
  elsif ($num_stars == 2)
  {
    $star_level = 2;

    if ($list_level < $num_stars) {
      print DEST "<$list_type>\n\n";
      $list_level = $num_stars;
    }
    elsif ($list_level > $num_stars) {
      print DEST "</$list_type>\n\n";
      $list_level = $num_stars;
    }

    print DEST "<li><strong>$_</strong>\n";
  }
  elsif ($num_stars == 3)
  {
    $star_level = 3;

    if ($list_level < $num_stars) {
      print DEST "<$list_type>\n\n";
      $list_level = $num_stars;
    }
    elsif ($list_level > $num_stars) {
      print DEST "</$list_type>\n\n";
      $list_level = $num_stars;
    }

    print DEST "<li>$_\n";
  }
  else
  {
    if ($star_level)
    {
      if (/^\s+/) {
        print DEST "<pre>\n" if (! $inside_pre);
        $inside_pre = 1;
      }
      elsif ($inside_pre) {
        print DEST "</pre>\n";
        $inside_pre = 0;
      }
    }

    if ((! $seen_first_heading) and (/^\s+/)) {
      print DEST "<center>$_</center>\n";
    }
    elsif ($quoted_mail_line) {
      print DEST "<font color=red>$_</font><br>\n";
    }
    elsif ((! $seen_first_heading) && ($_ =~ /^\s*[-_]+\s*$/)) {
      print DEST "<p><hr><p>\n";
    }
    elsif ($_ =~ /\S/) {
      print DEST "$_\n";
    }
    else {
      print DEST "\n\n<p>\n\n";
    }
  }
}

close (SOURCE);
close (DEST);


######################## Subroutines ############################

sub escape_html ()
{
  my $str = shift;
  $str =~ s/&/&amp;/g;
  $str =~ s/>/&gt;/g;
  $str =~ s/</&lt;/g;
  return $str;
}

sub expand_urls ()
{
  my $str = shift;

  while (1) {
    if ($str =~ s/([^>"])((http|https|ftp|nntp):\/\/[a-zA-Z0-9-_\/\.\~]+)/$1<a href="$2">$2<\/a>/) {
      ;
    }
    else {
      last;
    }
  }

  return $str;
}

sub interpret_asterisks_in_context ()
{
  my $str = shift;

  # Convert "*foo*" and similar things to "<i>foo</i>", and handle C
  # comments in inset code examples.

  if ($str =~ /^\s/)    # Inset text might be a monospaced code example
  {
    # Take care of C comments...
    $str =~ s/\/\*/<font color=blue>\/\*<i>/g;
    $str =~ s/\*\//<\/i>\*\/<\/font>/g;
  }
  else                  # Non-inset text could never be code example
  {
    # Take care of running text, ignoring C-style comments anyway,
    # since, who knows, they might appear in running text in a short
    # quote...
    $str =~ s/^\*([^*\s\/]+)/<i>$1/g;
    $str =~ s/(\s)\*([^*\s\/]+)/$1<i>$2/g;
    $str =~ s/([^*\s\/]+)\*$/$1<\/i>/g;
    $str =~ s/([^*\s\/]+)\*([\s\W\/])/$1<\/i>$2/g;
  }

  return $str;
}

sub count_stars ()
{
  my $str = shift;

  # Handles up to 9 stars.
  # 
  # todo: there's probably some way with Perl regexps to actually get
  #       the count and return it.  Or could just do it without
  #       regexps, but is it worth it?

  return 0 if ($str =~ /^[^*]/); # Common case -- this is not a star line
  return 1 if ($str =~ /^\* /);
  return 2 if ($str =~ /^\*\* /);
  return 3 if ($str =~ /^\*\*\* /);
  return 4 if ($str =~ /^\*\*\*\* /);
  return 5 if ($str =~ /^\*\*\*\*\* /);
  return 6 if ($str =~ /^\*\*\*\*\*\* /);
  return 7 if ($str =~ /^\*\*\*\*\*\*\* /);
  return 8 if ($str =~ /^\*\*\*\*\*\*\*\* /);
  return 9 if ($str =~ /^\*\*\*\*\*\*\*\*\* /);
}
