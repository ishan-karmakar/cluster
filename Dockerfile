FROM ubuntu:latest

WORKDIR /app

COPY build/cluster ./cluster

CMD ["./cluster"]