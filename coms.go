package main

import (
	"fmt"
	"io"
	"log"
	"net"
	"strings"
	"time"
)

func initInternalComs() {
	addr, err := net.ResolveTCPAddr("tcp", internalComPort)
	if err != nil {
		log.Fatalln(err)
	}
	conn, err := net.ListenTCP("tcp", addr)
	if err != nil {
		log.Fatalln(err)
	}

	log.Printf("Internal TCP server listening on %s...", internalComPort)
	handleInternalComs(conn)
}

func handleInternalComs(conn *net.TCPListener) {
	for {
		c, err := conn.AcceptTCP()
		if err != nil {
			log.Fatalln(err)
		}
		go handleInternalMessage(c)
	}
}

func handleInternalMessage(c *net.TCPConn) {
	defer c.Close()
	c.SetKeepAliveConfig(net.KeepAliveConfig{
		Enable:   true,
		Idle:     1000 * time.Second,
		Interval: 1000 * time.Second,
		Count:    5,
	})

	ip, _, err := net.SplitHostPort(c.RemoteAddr().String())
	if err != nil {
		log.Fatalln(err)
	}

	worker_id := len(nodes)
	if leader == nil {
		// old_nodes := "REG " + strconv.Itoa(worker_id)
		// for _, node := range nodes {
		// 	old_nodes += " " + node.address
		// 	node.conn.Write([]byte(fmt.Sprintf("ADD %s", ip)))
		// }
		// nodes = append(nodes, Node{
		// 	conn:    c,
		// 	address: ip,
		// })
		// c.Write([]byte(old_nodes))
		if len(nodes) == 0 {
			c.Write([]byte("NEXT"))
		} else {
			c.Write([]byte("NEXT " + nodes[0].address))
		}
		nodes = append(nodes, Node{})
		log.Printf("Registered new node (%s)\n", ip)
	}

	buffer := make([]byte, 64)
	for {
		l, err := c.Read(buffer)
		if err != nil {
			if err == io.EOF {
				if leader == nil {
					// Worker is dead
					removeWorker(worker_id)
					for _, node := range nodes {
						node.conn.Write([]byte(fmt.Sprintf("DEL %d", worker_id)))
					}
				} else {
					// Leader is dead
				}
			} else {
				log.Fatalln(err)
			}
		}
		action := string(buffer[:l])

		if strings.HasPrefix(action, "NEXT") {
			fields := strings.Fields(action)[1:]
			if len(fields) == 0 {
				// We are next in line
				next = ""
			} else {
				next = fields[0]
			}
		}

		// if strings.HasPrefix(action, "REG ") {
		// 	if leader == nil {
		// 		log.Fatalln("Received REG on leader")
		// 	}
		// 	fields := strings.Fields(action)[1:]
		// 	id, err = strconv.Atoi(fields[0])
		// 	if err != nil {
		// 		log.Fatalln(err)
		// 	}
		// 	// Here nodes should be empty
		// 	for _, node := range fields[1:] {
		// 		nodes = append(nodes, Node{
		// 			address: node,
		// 			conn:    nil,
		// 		})
		// 	}
		// 	log.Printf("We are registered with id %d\n", id)
		// }

		// if strings.HasPrefix(action, "ADD ") {
		// 	if leader == nil {
		// 		log.Fatalln("Received ADD on leader")
		// 	}
		// 	node := strings.Fields(action)[1]
		// 	nodes = append(nodes, Node{
		// 		address: node,
		// 		conn:    nil,
		// 	})
		// }

		// if strings.HasPrefix(action, "DEL ") {
		// 	if leader == nil {
		// 		log.Fatalln("Received DEL on leader")
		// 	}
		// 	idx, err := strconv.Atoi(strings.Fields(action)[1])
		// 	if err != nil {
		// 		log.Fatalln(err)
		// 	}
		// 	log.Println("Removing worker")
		// 	removeWorker(idx)
		// }
	}
}

func removeWorker(idx int) {
	log.Println(idx)
	nodes[idx] = nodes[len(nodes)-1]
	nodes = nodes[:len(nodes)-1]
}
