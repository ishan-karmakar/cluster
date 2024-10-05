package main

import (
	"encoding/json"
	"log"
	"math/rand/v2"
	"net"
	"os"
	"reflect"
	"time"
)

const internalComPort = ":6801"

var lastLogIndex = 0
var lastLogTerm = 0
var term = 0
var name = ""
var alreadyVoted = false
var peers []net.Conn

func main() {
	initConns()
	time.Sleep(time.Duration(rand.IntN(300-150)+150) * time.Millisecond)
	holdElection()
}

func initConns() {
	for _, peer := range os.Args[1:] {
		addr, _ := net.ResolveTCPAddr("tcp", peer+internalComPort)
		c, err := net.DialTCP("tcp", nil, addr)
		if err != nil {
			log.Fatalln(err)
		}
		if name == "" {
			name, _, _ = net.SplitHostPort(c.LocalAddr().String())
		}
		peers = append(peers, c)
	}
}

func holdElection() {
	if !alreadyVoted {
		for _, peer := range peers {
			msg := RequestVote{
				CurrentTerm:  term,
				LastLogIndex: lastLogIndex,
				LastLogTerm:  lastLogTerm,
				Candidate:    name,
			}
			sendRPC(msg, peer)
		}
	}
}

func initServer() {}

func sendRPC(msg any, conn net.Conn) {
	rpc, err := json.Marshal(RPC{
		Type: reflect.TypeOf(msg).Name(),
		Body: msg.(json.RawMessage),
	})
	if err != nil {
		log.Fatalln(err)
	}
	_, err = conn.Write(rpc)
	if err != nil {
		log.Fatalln(err)
	}
}
