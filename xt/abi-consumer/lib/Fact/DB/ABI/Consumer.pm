package Fact::DB::ABI::Consumer;

use 5.010;
use strict;
use warnings;

our $VERSION = '0.01';

require XSLoader;
XSLoader::load('Fact::DB::ABI::Consumer', $VERSION);

1;
