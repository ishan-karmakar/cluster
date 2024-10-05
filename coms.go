package main

import (
	"fmt"
	"log"
	"net"
	"strconv"
	"strings"
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
	// log.Printf("Received connection from %s\n", c.RemoteAddr())
	buffer := make([]byte, 64)
	for {
		l, err := c.Read(buffer)
		if err != nil {
			log.Fatalln(err)
		}
		action := string(buffer[:l])
		switch action {
		case "STATUS":
			c.Write([]byte("OK"))

		case "REG":
			if leader != nil {
				log.Fatalln("Received REG while not leader")
			}
			old_nodes := strconv.Itoa(len(nodes))
			ip := strings.Split(c.RemoteAddr().String(), ":")[0]
			for _, node := range nodes {
				old_nodes += " " + node.address
				node.conn.Write([]byte(fmt.Sprintf("ADD %s", ip)))
			}
			nodes = append(nodes, Node{
				conn:    c,
				address: ip,
			})
			c.Write([]byte(old_nodes))
			log.Printf("Registered new node (%s)\n", ip)
		}

		if strings.HasPrefix(action, "ADD ") {
			if leader == nil {
				log.Fatalln("Received ADD on leader")
			}
			node := strings.Fields(action)[1]
			nodes = append(nodes, Node{
				address: node,
				conn:    nil,
			})
			log.Println("Added new node to list")
		}
	}
}
