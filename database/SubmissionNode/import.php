<script language="php">
/* This file contains opensource code from outside sources and is provided to 
Altair Engineering under the following open source license:

Copyright (c) 2015, Harry W. Waddell
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

function usage()
{
  printf("usage: php import.php filename id sha256 submitter role\n");
  exit(1);
}

openlog("wrappers", LOG_ODELAY, LOG_AUTHPRIV);

//using local connection, with peer auth and a map file
$pg_connect_args = "user=wrapper dbname=wrapper";

if(!($PG = pg_pconnect($pg_connect_args))){
  syslog(LOG_CRIT, "could not connect to the database");
  printf("error: could not connect to the object database\n");
  exit(1);
}

if( $argc != 6 ){
  usage();
}

$object_path = $argv[1];
$id = $argv[2];
$sum = $argv[3];
$submitter = $argv[4];
$role = $argv[5];

if(! ($buffer = file_get_contents($object_path))){
  printf("error: could not open %s\n", $object_path);
  exit(1);
}

$dsize = strlen($buffer);

$query = sprintf("INSERT into wrapped_object (id, data, login, dsize, ctime, chksum, orole ) VALUES ".
		 "('%s', '%s', '%s', %d, 'now', '%s', '%s')",
		 $id, pg_escape_bytea ($PG, $buffer), $submitter, $dsize, $sum, $role );
		 
if(!($result = pg_query($PG, $query))){
  syslog(LOG_CRIT, $pg_result_error($result));
  printf("FATAL ERROR: db insert command failed\n");
  exit(1);
}

exit(0);


</script>
