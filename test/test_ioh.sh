#!/usr/bin/perl
# $Id: test_ioh.sh,v 1.3 2003/09/05 18:41:46 seth Exp $
#
# ++Copyright LIBBK++
#
# Copyright (c) 2003 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Mail <projectbaka@baka.org> for further information
#
# --Copyright LIBBK--
#
#



$receive{'ttcp'} = 'ttcp -p $port -r > /proj/startide/seth/big.out';
$trans{'ttcp'} = 'ttcp -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'bdttcp'} = 'bdttcp -p $port -r 127.1 < /dev/null > /proj/startide/seth/big.out';
$trans{'bdttcp'} = 'bdttcp -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh'} = 'test_ioh --ioh-inbuf-hint=8192 -p $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh'} = 'test_ioh --ioh-inbuf-hint=8192 -p $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh-S'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh-S'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -t 127.1 < /proj/startide/seth/big.in';
$receive{'test_ioh-Sl'} = 'test_ioh --ioh-inbuf-hint=8192 -Sp $port -r < /dev/null > /proj/startide/seth/big.out';
$trans{'test_ioh-Sl'} = 'test_ioh --ioh-outbuf-max=20000 --ioh-inbuf-hint=8192 -Sp $port -t 127.1 < /proj/startide/seth/big.in';

$ENV{'port'}=6010;

foreach $cmd ('ttcp','ttcp','ttcp','bdttcp','bdttcp','bdttcp','test_ioh','test_ioh','test_ioh','test_ioh-S','test_ioh-S','test_ioh-S','test_ioh-Sl','test_ioh-Sl','test_ioh-Sl')
{
  print "$cmd $receive{$cmd} &\n";
  system("$receive{$cmd} &");
  sleep(5);
  system("time $trans{$cmd}");
  $ENV{'port'}++;
}
