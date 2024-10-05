package main

import (
	"log"
	"net"
	"os"
)

const internalComPort = ":6801"

type Node struct {
	address string
	conn    net.Conn
}

var leader *net.TCPConn = nil
var next string
var nodes []Node

func main() {
	if len(os.Args) == 1 {
		log.Println("We are the leader")
		initInternalComs()
	} else {
		log.Printf("%s is the leader\n", os.Args[1])
		var err error
		addr, err := net.ResolveTCPAddr("tcp", os.Args[1]+internalComPort)
		if err != nil {
			log.Fatalln(err)
		}
		for ok := true; ok; ok = err != nil {
			leader, err = net.DialTCP("tcp", nil, addr)
		}
		handleInternalMessage(leader)
	}
}
