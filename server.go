package main

import (
	"io"
	"log"
	"net"
)

func initServer() {
	addr, err := net.ResolveTCPAddr("tcp", internalComPort)
	if err != nil {
		log.Fatalln(err)
	}

	conn, err := net.ListenTCP("tcp", addr)
	if err != nil {
		log.Fatalln(err)
	}

	for {
		c, err := conn.AcceptTCP()
		if err != nil {
			log.Fatalln(err)
		}

		go handleConnection(c)
	}
}

func handleConnection(c *net.TCPConn) {
	for {
		rpc, err := getRPC(c)
		if err == io.EOF {
			return
		} else if err != nil {
			log.Fatalln(err)
		}

		if term < rpc.Term {
			role = FOLLOWER
			term = rpc.Term
		}

		// Leader doesn't need to process messages, just needs the term
		if role == LEADER {
			continue
		}

		switch rpc.Type {
		case "AppendEntries":
			heartbeatEvent <- struct{}{}

		case "RequestVote":
			body := castRPC[RequestVote](rpc)
			if rpc.Term < term || alreadyVoted {
				sendRPC(RequestVoteResponse(false), c)
			} else if body.LastLogTerm > lastLogTerm || (body.LastLogTerm == lastLogTerm && body.LastLogIndex >= lastLogIndex) {
				alreadyVoted = true
				sendRPC(RequestVoteResponse(true), c)
			} else {
				sendRPC(RequestVoteResponse(false), c)
			}
		}
	}
}
