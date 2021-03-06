#!/bin/sh

ECHO="echo"
LN="ln"
MKDIR="mkdir"
RMF="rm -f"
RMRF="rm -rf"
TAR="tar"
WGET="wget"

# parse arguments
helpFlag=0
cleanFlag=0
while test $# != 0; do
  if   test "$1" = "--help"; then
    helpFlag=1
  elif test "$1" = "--clean"; then
    cleanFlag=1
  else
    $ECHO >&2 "ERROR: unknown option '$1'"
    exit 1
  fi
  shift
done
if test $helpFlag -eq 1; then
  $ECHO "download-third-party-packages.sh [--clean] [--help]"
  exit 0
fi

# run
tmpDirectory="packages"
cwd=`pwd`
if test $cleanFlag -eq 0; then
  $MKDIR $tmpDirectory 2>/dev/null

  # zlib
  (
   cd $tmpDirectory
   if test ! -f zlib-1.2.3.tar.gz; then
     $WGET 'http://www.zlib.net/zlib-1.2.3.tar.gz'
   fi
   $TAR xzf zlib-1.2.3.tar.gz
  )
  $LN -f -s $tmpDirectory/zlib-1.2.3 zlib

  # bzip2
  (
   cd $tmpDirectory
   if test ! -f bzip2-1.0.5.tar.gz; then
     $WGET 'http://www.bzip.org/1.0.5/bzip2-1.0.5.tar.gz'
   fi
   $TAR xzf bzip2-1.0.5.tar.gz
  )
  $LN -f -s $tmpDirectory/bzip2-1.0.5 bzip2

  # lzma
  (
   cd $tmpDirectory
   fileName=`ls xz-*.tar.gz 2>/dev/null`
   if test ! -f "$fileName"; then
     fileName=`wget --quiet -O - 'http://tukaani.org/xz'|grep -E -e 'xz-.*\.tar\.gz'|sed 's|.*href="\(xz.*\.tar\.gz\)".*|\1|g'`     
     $WGET "http://tukaani.org/xz/$fileName"
   fi
   $TAR xzf $fileName
  )
  $LN -f -s `find $tmpDirectory -type d -name "xz-*"` xz

  # gcrypt
  (
   cd $tmpDirectory
   if test ! -f libgpg-error-1.7.tar.bz2; then
     $WGET 'ftp://ftp.gnupg.org/gcrypt/libgpg-error/libgpg-error-1.7.tar.bz2'
   fi
   if test ! -f libgcrypt-1.4.4.tar.bz2; then
     $WGET 'ftp://ftp.gnupg.org/gcrypt/libgcrypt/libgcrypt-1.4.4.tar.bz2'
   fi
   $TAR xjf libgpg-error-1.7.tar.bz2
   $TAR xjf libgcrypt-1.4.4.tar.bz2
  )
  $LN -f -s $tmpDirectory/libgpg-error-1.7 libgpg-error
  $LN -f -s $tmpDirectory/libgcrypt-1.4.4 libgcrypt

  # ftplib
  (
   cd $tmpDirectory
   if test ! -f ftplib-3.1-src.tar.gz; then
     $WGET 'http://www.nbpfaus.net/~pfau/ftplib/ftplib-3.1-src.tar.gz'
   fi
   if test ! -f ftplib-3.1-1.patch; then
     $WGET 'http://nbpfaus.net/~pfau/ftplib/ftplib-3.1-1.patch'
   fi
   $TAR xzf ftplib-3.1-src.tar.gz
   (cd ftplib-3.1; patch -p3 < ../ftplib-3.1-1.patch)
  )
  $LN -f -s $tmpDirectory/ftplib-3.1 ftplib

  # libssh2
  (
   cd $tmpDirectory
   if test ! -f libssh2-1.1.tar.gz; then
     $WGET 'http://www.libssh2.org/download/libssh2-1.2.2.tar.gz'
   fi
   $TAR xzf libssh2-1.2.2.tar.gz
  )
  $LN -f -s $tmpDirectory/libssh2-1.2.2 libssh2

  # gnutls
  (
   cd $tmpDirectory
   if test ! -f gnutls-2.8.1.tar.bz2; then
     $WGET 'ftp://ftp.gnu.org/pub/gnu/gnutls/gnutls-2.8.1.tar.bz2'
   fi
   $TAR xjf gnutls-2.8.1.tar.bz2
  )
  $LN -f -s $tmpDirectory/gnutls-2.8.1 gnutls
else
  # bzip2
  $RMF $tmpDirectory/bzip2-1.0.5.tar.gz
  $RMRF $tmpDirectory/bzip2-1.0.5
  $RMF bzip2

  # lzma
  $RMF `find $tmpDirectory -type f -name "xz-*.tar.gz" 2>/dev/null`
  $RMRF `find $tmpDirectory -type d -name "xz-*" 2>/dev/null`
  $RMF xz

  # gcrypt
  $RMF $tmpDirectory/libgpg-error-1.7.tar.bz2 $tmpDirectory/libgcrypt-1.4.4.tar.bz2
  $RMRF $tmpDirectory/libgpg-error-1.7 $tmpDirectory/libgcrypt-1.4.4
  $RMF libgpg-error libgcrypt

  # ftplib
  $RMF $tmpDirectory/ftplib-3.1-src.tar.gz $tmpDirectory/ftplib-3.1-1.patch
  $RMRF $tmpDirectory/ftplib-3.1
  $RMF ftplib

  # libssh2
  $RMF $tmpDirectory/libssh2-1.1.tar.gz
  $RMRF $tmpDirectory/libssh2-1.1
  $RMF libssh2

  # gnutls
  $RMF $tmpDirectory/gnutls-2.8.1.tar.bz2
  $RMRF $tmpDirectory/gnutls-2.8.1
  $RMF gnutls
fi

exit 0
