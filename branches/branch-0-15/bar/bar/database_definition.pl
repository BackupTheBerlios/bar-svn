#!/usr/bin/perl

#!/bin/sh
#\
#exec perl "$0" "$@"

# ----------------------------------------------------------------------------
#
# $Source: /home/torsten/cvs/bar/bar/database_definition.pl,v $
# $Revision: 1.1.2.1 $
# $Author: torsten $
# Contents: create header file definition from database definition
# Systems: all
#
# ----------------------------------------------------------------------------

# ---------------------------- additional packages ---------------------------
use English;
use POSIX;
use Getopt::Std;
use Getopt::Long;

# ---------------------------- constants/variables ---------------------------

# --------------------------------- includes ---------------------------------

# -------------------------------- functions ---------------------------------

# ------------------------------ main program  -------------------------------

my $commentFlag=0;
print "#define DATABASE_TABLE_DEFINITION \\\n";
print "\"\\\n";
while ($line=<STDIN>)
{
  chop $line;
  if ($line =~ /^\s*\/\// || $line =~ /^\s*$/) { next; }

  if ($commentFlag)
  {
    if ($line =~ /\*\/(.*)/)
    {
      $line=$1;
      $commentFlag=0;
    }
    else
    {
      $line="";
    }
  }
  else
  {
    if ($line =~ /(.*)\/\*/)
    {
      $line=$1;
      $commentFlag=1;
    }
  }

  if ($line ne "")
  {
    print "$line\\\n";
  }
}
print "\"\n";

exit 0;
# end of file
