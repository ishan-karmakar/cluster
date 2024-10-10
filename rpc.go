package main

import (
	"encoding/json"
	"log"
	"net"
	"reflect"
)

func sendRPC(msg any, conn net.Conn) {
	serialized, err := json.Marshal(msg)
	if err != nil {
		log.Fatalln(err)
	}
	rpc, err := json.Marshal(RPC{
		Term: term,
		Type: reflect.TypeOf(msg).Name(),
		Body: serialized,
	})
	if err != nil {
		log.Fatalln(err)
	}
	_, err = conn.Write(rpc)
	if err != nil {
		log.Fatalln(err)
	}
}

func getRPC(conn net.Conn) (RPC, error) {
	var response RPC
	buf := make([]byte, 128)
	n, err := conn.Read(buf)
	if err != nil {
		return RPC{}, err
	}
	err = json.Unmarshal(buf[:n], &response)
	if err != nil {
		log.Fatalln(err)
	}
	return response, nil
}

func castRPC[T any](rpc RPC) T {
	var inner T
	err := json.Unmarshal(rpc.Body, &inner)
	if err != nil {
		log.Fatalln(err)
	}
	return inner
}
