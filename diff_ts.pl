#!/usr/bin/perl -w
# This script reads a TeSSLa stream and converts complex events (CEPs) where
#  multiple events trigger on the same timestamp into a trace with increasing
#  timestamps.
# Author: vs
use bigint;

$c = 0;
$old = -1;
$vts = -1;
while (<>) {
  if (/^(\d*): (.*)/) {
    $ts = $1;
    if ($ts > $old) {
      die if ($vts >= $ts); # previous CEP overran
      $old = $ts;
      $c = 0;
    }
    $vts = $ts+$c;
    $c=$c+1;
    print "$vts: $2\n"
  }
}
