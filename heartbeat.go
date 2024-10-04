package main

import (
	"log"
	"net"
)

func initHeartbeat() {
	addr, err := net.ResolveUDPAddr("udp", heartbeatPort)
	if err != nil {
		log.Fatalln(err)
	}

	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		log.Fatalln(err)
	}

	log.Printf("Heartbeat listening on %s...", heartbeatPort)
	go handleHeartbeatChecks(conn)
}

func handleHeartbeatChecks(conn *net.UDPConn) {
	for {
		_, addr, err := conn.ReadFrom(nil)
		if err != nil {
			log.Fatalln(err)
		}
		_, err = conn.WriteTo([]byte("UP"), addr)
		if err != nil {
			log.Fatalln(err)
		}
		log.Println("Received heartbeat check from", addr)
	}
}

func isNodeUp(node net.Conn) bool {
	// response := make([]byte, 2)
	// for i := 0; i < NUM_RETRIES; i++ {
	// 	if func() bool {
	// 		node.Write([]byte{})
	// 		node.SetReadDeadline(time.Now().Add(50 * time.Millisecond))
	// 		_, err := node.Read(response[0:])
	// 		if err != nil {
	// 			time.Sleep(50 * time.Millisecond) // Connection refused
	// 			return false
	// 		}

	// 		return bytes.Equal(response[0:], []byte("UP"))
	// 	}() {
	// 		return true
	// 	}
	// }
	return false
}
