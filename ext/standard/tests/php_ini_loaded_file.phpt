--TEST--
Check the php_ini_loaded_file() function
--XFAIL--
b0rk3n
--CREDITS--
Sebastian Schürmann
sschuermann@chip.de
Testfest 2009 Munich
--INI--
precision=12
--FILE--
<?php
var_dump(php_ini_loaded_file());
?>
--EXPECTF--
string(%d) "%sphp.ini"
