package main

import (
	"log"
	"net"
)

const HEARTBEAT_ADDR = "localhost:6801"

func main() {
	addr, err := net.ResolveUDPAddr("udp", HEARTBEAT_ADDR)
	if err != nil {
		log.Fatalln(err)
	}

	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		log.Fatalln(err)
	}

	handleHeartbeat(conn)
}

func handleHeartbeat(conn *net.UDPConn) {
	log.Printf("Heartbeat listening on %s...", HEARTBEAT_ADDR)
	for {
		_, addr, err := conn.ReadFromUDP(nil)
		if err != nil {
			log.Fatalln(err)
		}
		log.Println("Received heartbeat check from", addr)
	}
}
