package main

import (
	"log"
	"net"
)

func main() {
	udpAddr, err := net.ResolveUDPAddr("udp", "localhost:6801")
	if err != nil {
		log.Fatalln(err)
	}

	conn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		log.Fatalln(err)
	}

	go handleUDP(conn)

}

func handleUDP(conn *net.UDPConn) {
	n, addr, err := conn.ReadFromUDP(nil)
	if err != nil {
		log.Fatalln(err)
	}
	log.Println(n, addr)
}
