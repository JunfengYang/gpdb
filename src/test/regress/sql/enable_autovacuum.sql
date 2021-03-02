-- start_ignore
\! gpconfig -c autovacuum -v 'on' --skipvalidation;
\! gpstop -ari;
-- end_ignore
\! psql postgres -c "show autovacuum;";
