\getenv replication_password POSTGRES_REPLICATION_PASSWORD
SELECT 'CREATE ROLE mini_checkers_repl WITH LOGIN REPLICATION'
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'mini_checkers_repl')
\gexec
SELECT format('ALTER ROLE mini_checkers_repl PASSWORD %L', :'replication_password')
\gexec
