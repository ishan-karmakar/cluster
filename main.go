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

var leader net.Conn = nil
var nodes []Node

func main() {
	go initialize()
	initInternalComs()
}

func initialize() {
	if len(os.Args) == 1 {
		log.Println("We are the leader")
	} else {
		log.Printf("%s is the leader\n", os.Args[1])
		var err error
		leader, err = net.Dial("tcp", os.Args[1]+internalComPort)
		for err != nil {
			log.Println("Retry leader connection again...")
			leader, err = net.Dial("tcp", os.Args[1]+internalComPort)
		}
		log.Println("Connected to leader!")
		leader.Write([]byte("REG"))
	}
}
