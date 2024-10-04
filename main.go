package main

import (
	"log"
	"os"
)

const HEARTBEAT_ADDR = "0.0.0.0:6801"
const NUM_RETRIES = 2

var LEADER string = ""

func main() {
	handleHeartbeat() // As soon as we start, make sure others know we are up
	if len(os.Args) == 1 {
		log.Println("We are the leader")
	} else {
		log.Printf("%s is the leader\n", os.Args[1])
		LEADER = os.Args[1]
	}
}

// func convertNodes(nodes []string) []net.Conn {
// 	conns := make([]net.Conn, len(nodes))
// 	for i, ip := range nodes {
// 		conn, err := net.Dial("udp", ip)
// 		if err != nil {
// 			log.Fatalln(err)
// 		}
// 		conns[i] = conn
// 	}
// 	sort.Slice(conns, func(i, j int) bool {
// 		return nodes[i] < nodes[j]
// 	})
// 	return conns
// }
