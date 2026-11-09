FROM postgres:17-bookworm

COPY replication.conf pg_hba.conf /etc/postgresql/
COPY replication-role.sql /usr/local/share/mini-checkers/replication-role.sql
COPY --chmod=755 start-postgres.sh postgres-maintenance.sh /usr/local/bin/
COPY --chmod=755 init-replication-config.sh /docker-entrypoint-initdb.d/00-replication-config.sh

ENTRYPOINT ["/usr/local/bin/start-postgres.sh"]
CMD ["postgres"]
