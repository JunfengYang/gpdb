!\retcode gpconfig -c autovacuum -v on --skipvalidation;
!\retcode gpstop -ari;
! psql postgres -c "show autovacuum;";
