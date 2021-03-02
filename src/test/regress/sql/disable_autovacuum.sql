-- start_ignore
\! gpconfig -c autovacuum -v off --skipvalidation;
\! gpstop -ari;
-- end_ignore
\! psql postgres -c "show autovacuum;";
