FROM rabbitmq:4.1

COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf
COPY --chmod=755 start-rabbitmq.sh /usr/local/bin/start-rabbitmq.sh
ENTRYPOINT ["/usr/local/bin/start-rabbitmq.sh"]
CMD ["rabbitmq-server"]
