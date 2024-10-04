package main

import (
	"fmt"
	"log"
	"net"
)

func initInternalComs() {
	conn, err := net.Listen("tcp", internalComPort)
	if err != nil {
		log.Fatalln(err)
	}

	log.Printf("Internal TCP server listening on %s...", internalComPort)
	handleInternalComs(conn)
}

func handleInternalComs(conn net.Listener) {
	for {
		c, err := conn.Accept()
		if err != nil {
			log.Fatalln(err)
		}
		go handleInternalMessage(c)
	}
}

func handleInternalMessage(c net.Conn) {
	log.Printf("Received connection from %s\n", c.RemoteAddr())
	buffer := make([]byte, 64)
	for {
		l, err := c.Read(buffer)
		if err != nil {
			log.Fatalln(err)
		}
		switch string(buffer[:l]) {
		case "STATUS":
			c.Write([]byte("OK"))

		case "REG":
			if leader != nil {
				log.Fatalln("Received REG while not leader")
			}
			var old_nodes string
			for _, node := range nodes {
				old_nodes += fmt.Sprintf("%s ", node.RemoteAddr())
				node.Write([]byte(fmt.Sprintf("ADD %s", c.RemoteAddr())))
			}
			nodes = append(nodes, c)
			c.Write([]byte(old_nodes))
			log.Printf("Registered new node (%s)\n", c.RemoteAddr())
		}
	}
}
