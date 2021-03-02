!\retcode gpconfig -c autovacuum -v off --skipvalidation;
!\retcode gpstop -ari;
! psql postgres -c "show autovacuum;";
