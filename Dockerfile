FROM golang:latest

WORKDIR /app
COPY *.go go.mod go.sum* ./
RUN go mod download
RUN go build -o /cluster
EXPOSE 6801/udp
ENTRYPOINT ["/cluster"]