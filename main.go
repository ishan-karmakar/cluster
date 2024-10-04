package main

import (
	"log"
	"net"
	"os"
)

const heartbeatPort = ":6802"
const internalComPort = ":6801"

var leader net.Conn = nil
var nodes []net.Conn

func main() {
	// initHeartbeat() // As soon as we start, make sure others know we are up
	initInternalComs()
	if len(os.Args) == 1 {
		log.Println("We are the leader")
	} else {
		log.Printf("%s is the leader\n", os.Args[1])
	}
}
