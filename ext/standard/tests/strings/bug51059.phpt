--TEST--
Bug #51059 crypt() segfaults on certain salts
--XFAIL--
b0rk3n
--FILE--
<?php
$res = crypt(b'a', b'_');
if ($res === b'__DAZ.Z4ErJDo') echo 'OK';
else echo 'Not OK';

?>
--EXPECT--
OK
