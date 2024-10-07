package main

import (
	"encoding/json"
	"io"
	"log"
	"math/rand/v2"
	"net"
	"os"
	"reflect"
	"time"
)

const internalComPort = ":6801"
const (
	LEADER    = iota
	FOLLOWER  = iota
	CANDIDATE = iota
)
const ELECTION_TIMEOUT_MIN = 300
const ELECTION_TIMEOUT_MAX = 1000

var term = 0
var lastLogIndex = 0
var lastLogTerm = 0
var numVotes int
var alreadyVoted = false
var lastHearbeat time.Time
var electionTimeout time.Duration
var peers = make(map[string]*net.TCPConn)
var role = FOLLOWER

// {"Type":"RequestVote","Body":{"LastLogIndex":0,"LastLogTerm":0,"Candidate":"127.0.0.1"}}
func main() {
	initConns()
	checkTimeout()
}

func initConns() {
	for _, peer := range os.Args[1:] {
		peers[peer] = nil
	}
}

// Lazily initializes the connections
func getConn(peer string) *net.TCPConn {
	conn := peers[peer]
	if conn != nil {
		return conn
	} else {
		addr, _ := net.ResolveTCPAddr("tcp", peer+internalComPort)
		c, err := net.DialTCP("tcp", nil, addr)
		if err != nil {
			log.Fatalln(err)
		}
		peers[peer] = c
		return c
	}
}

func checkTimeout() {
	lastHearbeat = time.Now()
	setElectionTimeout()
	go func() {
		time.Sleep(50 * time.Millisecond)
		if (time.Now() - lastHearbeat)
	}()
}

func setElectionTimeout() {
	electionTimeout = time.Duration(rand.IntN(ELECTION_TIMEOUT_MAX-ELECTION_TIMEOUT_MIN)+ELECTION_TIMEOUT_MIN) * time.Millisecond
}

func holdElection() {
	if !alreadyVoted {
		numVotes = 0 // Vote for self
		term++
		role = CANDIDATE
		msg := RequestVote{
			Term:         term,
			LastLogIndex: lastLogIndex,
			LastLogTerm:  lastLogTerm,
		}
		log.Println("I am a candidate")
		for peer := range peers {
			c := getConn(peer)
			go candidateHandleMessage(c)
			sendRPC(msg, c)
		}
	}
}

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

		go serverHandleMessage(c)
	}
}

func candidateHandleMessage(c *net.TCPConn) {
	defer c.Close()

	rpc := getRPC(c).(RequestVoteResponse)
	log.Println(rpc.VoteGranted)
	if rpc.VoteGranted {
		if numVotes >= len(peers)/2+1 && role == CANDIDATE { // serverHandleMessage could put us back into follower
			log.Println("We are the leader now")
		}
	}
}

func serverHandleMessage(c *net.TCPConn) {
	defer c.Close()
	c.SetKeepAliveConfig(net.KeepAliveConfig{
		Enable:   true,
		Idle:     1000 * time.Second,
		Interval: 1000 * time.Second,
		Count:    5,
	})

	for {
		rpc := getRPC(c)
		if rpc == nil {
			return
		}
		switch rpc := rpc.(type) {
		case RequestVote:
			log.Println("Received RequestVote")
			if (role == CANDIDATE && rpc.Term >= term) || (role == FOLLOWER && !alreadyVoted && rpc.LastLogTerm >= lastLogTerm) {
				role = FOLLOWER // Recognize other candidate as legitimate if applicable
				alreadyVoted = true
				sendRPC(RequestVoteResponse{
					Term:        term,
					VoteGranted: true,
				}, c)
			} else {
				sendRPC(RequestVoteResponse{
					Term:        term,
					VoteGranted: false,
				}, c)
			}
		}
	}
}

func sendRPC(msg any, conn net.Conn) {
	serialized, err := json.Marshal(msg)
	if err != nil {
		log.Fatalln(err)
	}
	rpc, err := json.Marshal(RPC{
		Type: reflect.TypeOf(msg).Name(),
		Body: serialized,
	})
	if err != nil {
		log.Fatalln(err)
	}
	_, err = conn.Write(rpc)
	if err != nil {
		log.Fatalln(err)
	}
}

// TODO: Fix awful switch statement
func getRPC(conn net.Conn) any {
	var response RPC
	buf := make([]byte, 128)
	n, err := conn.Read(buf)
	if err == io.EOF {
		return nil
	} else if err != nil {
		log.Fatalln(err)
	}
	err = json.Unmarshal(buf[:n], &response)
	if err != nil {
		log.Fatalln(err)
	}

	switch response.Type {
	case "RequestVote":
		var body RequestVote
		err = json.Unmarshal(response.Body, &body)
		if err != nil {
			log.Fatalln(err)
		}
		return body

	case "RequestVoteResponse":
		var body RequestVoteResponse
		err = json.Unmarshal(response.Body, &body)
		if err != nil {
			log.Fatalln(err)
		}
		return body
	}
	return nil
}
