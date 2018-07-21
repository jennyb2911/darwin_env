use strict;
use lib qw(t/lib);
use dbixcsl_common_tests;

my $dsn      = $ENV{DBICTEST_DB2_DSN} || '';
my $user     = $ENV{DBICTEST_DB2_USER} || '';
my $password = $ENV{DBICTEST_DB2_PASS} || '';

my $tester = dbixcsl_common_tests->new(
    vendor         => 'DB2',
    auto_inc_pk    => 'INTEGER GENERATED BY DEFAULT AS IDENTITY NOT NULL PRIMARY KEY',
    default_function => 'CURRENT TIMESTAMP',
    dsn            => $dsn,
    user           => $user,
    password       => $password,
    null           => '',
);

if( !$dsn || !$user ) {
    $tester->skip_tests('You need to set the DBICTEST_DB2_DSN, _USER, and _PASS environment variables');
}
else {
    $tester->run_tests();
}
