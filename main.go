package main

import (
	"log"
	"math/rand/v2"
	"net"
	"os"
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
const REELECTION_TIMEOUT = 1500

var term = 0
var lastLogIndex = 0
var lastLogTerm = 0
var numVotes int
var alreadyVoted = false
var electionTimeout time.Duration
var peers = make(map[string]*net.TCPConn)
var role = FOLLOWER
var roleChange = make(chan struct{})
var heartbeatEvent = make(chan struct{})
var leaderEvent = make(chan struct{})

// {"Type":"RequestVote","Body":{"LastLogIndex":0,"LastLogTerm":0,"Candidate":"127.0.0.1"}}
func main() {
	initConns()
	go checkHeartbeat()
	initServer()
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

func setElectionTimeout() {
	electionTimeout = time.Duration(rand.IntN(ELECTION_TIMEOUT_MAX-ELECTION_TIMEOUT_MIN)+ELECTION_TIMEOUT_MIN) * time.Millisecond
}
