#!/usr/bin/perl

#!/bin/sh
#\
#exec perl "$0" "$@"

# ----------------------------------------------------------------------------
#
# $Source: /home/torsten/cvs/bar/bar/archive_format.pl,v $
# $Revision: 1.2 $
# $Author: torsten $
# Contents: create header/c file definition from format definition
# Systems : all
#
# ----------------------------------------------------------------------------
# Exported Functions
#
# ----------------------------------------------------------------------------
# Function                       Purpose
# ----------------------------------------------------------------------------
#
# ----------------------------------------------------------------------------

# ---------------------------- additional packages ---------------------------
use English;
use POSIX;
use Getopt::Std;
use Getopt::Long;

# ---------------------------- constants/variables ---------------------------

my $PREFIX_ID         = "CHUNK_ID_";
my $PREFIX_NAME       = "Chunk";
my $PREFIX_DEFINITION = "CHUNK_DEFINITION_";

my $DEFINITION_TYPES =
  {
   "uint8"  => "CHUNK_DATATYPE_UINT8",
   "uint16" => "CHUNK_DATATYPE_UINT16",
   "uint32" => "CHUNK_DATATYPE_UINT32",
   "uint64" => "CHUNK_DATATYPE_UINT64",
   "int8"   => "CHUNK_DATATYPE_INT8",
   "int16"  => "CHUNK_DATATYPE_INT16",
   "int32"  => "CHUNK_DATATYPE_INT32",
   "int64"  => "CHUNK_DATATYPE_INT64",
   "string" => "CHUNK_DATATYPE_STRING",
   "data"   => "CHUNK_DATATYPE_DATA",
   "crc32"  => "CHUNK_DATATYPE_CRC32",
  };

my $cFileName,$hFileName;

# --------------------------------- includes ---------------------------------

# -------------------------------- functions ---------------------------------

sub writeHFile($)
{
  my $s=shift(@_);

  if ($hFileName ne "")
  {
    print HFILE_HANDLE $s;
  }
}

sub writeCFile($)
{
  my $s=shift(@_);

  if ($cFileName ne "")
  {
    print CFILE_HANDLE $s;
  }
}

# ------------------------------ main program  -------------------------------

GetOptions("c=s" => \$cFileName,
           "h=s" => \$hFileName,
           "i=s" => \$includeFileName,
          );

if ($cFileName ne "")
{
  open(CFILE_HANDLE,"> $cFileName");
}
if ($hFileName ne "")
{
  open(HFILE_HANDLE,"> $hFileName");
  print HFILE_HANDLE "#ifndef __ARCHIVE_FORMAT__\n";
  print HFILE_HANDLE "#define __ARCHIVE_FORMAT__\n";
}

writeCFile("#include \"chunks.h\"\n");
if ($includeFileName ne "") { writeCFile("#include \"$includeFileName\"\n"); }

my $line;
my $lineNb=0;
while ($line=<STDIN>)
{
  chop $line;
  $lineNb++;
  if ($line =~ /^\s*#/ || $line =~ /^\s*$/) { next; }
#print "$line\n";

  if ($line =~ /^CHUNK\s+(\w+)\s+"(.*)"\s+(\w+)/)
  {
    # chunk
    my $n=0;
    my $idName=$1;
    my $id=$2;
    my $structName=$3;
    my @parseDefinitions;

    # Note: use padding in C structures for access via pointer

    writeHFile("#define $PREFIX_ID$idName (('".substr($id,0,1)."' << 24) | ('".substr($id,1,1)."' << 16) | ('".substr($id,2,1)."' << 8) | '".substr($id,3,1)."')\n");
    writeHFile("typedef struct {\n");
    while ($line=<STDIN>)
    {
      chop $line;
      $lineNb++;
      if ($line =~ /^\s*#/ || $line =~ /^\s*$/) { last; }

      if    ($line =~ /^\s*(uint8|int8)\s+(\w+)/)
      {
        writeHFile("  $1 $2;\n");
        writeHFile("  uint8 pad".$n."[3];\n");
        push(@parseDefinitions,$DEFINITION_TYPES->{$1});
        push(@parseDefinitions,"offsetof($PREFIX_NAME$structName,$2)");
      }
      elsif ($line =~ /^\s*(uint16|int16)\s+(\w+)/)
      {
        writeHFile("  $1 $2;\n");
        writeHFile("  uint8 pad".$n."[2];\n");
        push(@parseDefinitions,$DEFINITION_TYPES->{$1});
        push(@parseDefinitions,"offsetof($PREFIX_NAME$structName,$2)");
      }
      elsif ($line =~ /^\s*(uint32|int32)\s+(\w+)/)
      {
        writeHFile("  $1 $2;\n");
        push(@parseDefinitions,$DEFINITION_TYPES->{$1});
        push(@parseDefinitions,"offsetof($PREFIX_NAME$structName,$2)");
      }
      elsif ($line =~ /^\s*(uint64|int64)\s+(\w+)/)
      {
        writeHFile("  $1 $2;\n");
        push(@parseDefinitions,$DEFINITION_TYPES->{$1});
        push(@parseDefinitions,"offsetof($PREFIX_NAME$structName,$2)");
      }
      elsif ($line =~ /^\s*string\s+(\w+)/)
      {
        writeHFile("  String $1;\n");
        push(@parseDefinitions,$DEFINITION_TYPES->{string});
        push(@parseDefinitions,"offsetof($PREFIX_NAME$structName,$1)");
      }
      elsif ($line =~ /^\s*data\s+(\w+)/)
      {
        push(@parseDefinitions,$DEFINITION_TYPES->{data});
        push(@parseDefinitions,"0");
      }
      elsif ($line =~ /^\s*crc32\s+(\w+)/)
      {
        push(@parseDefinitions,$DEFINITION_TYPES->{crc32});
        push(@parseDefinitions,"0");
      }
      else
      {
        print STDERR "Unknown data '$line' in line $lineNb\n";
        exit 1;
      }

      $n++;
    }
    writeHFile("} $PREFIX_NAME$structName;\n");
    push(@parseDefinitions,"0");
    writeHFile("extern int $PREFIX_DEFINITION$idName\[\];\n");
    writeCFile("int $PREFIX_DEFINITION$idName\[\] = {".join(",",@parseDefinitions)."};\n");
  }
  else
  {
    print STDERR "Unknown data '$line' in line $lineNb\n";
    exit 1;
  }
}

if ($cFileName ne "")
{
  close(CFILE_HANDLE);
}
if ($hFileName ne "")
{
  print HFILE_HANDLE "#endif /* __ARCHIVE_FORMAT__ */\n";
  close(HFILE_HANDLE);
}

exit 0;
# end of file
