package main

import (
	"log"
	"net"
	"time"
)

func checkHeartbeat() {
	for {
		if role == LEADER {
			for peer := range peers {
				c := getConn(peer)
				sendRPC(AppendEntries{}, c)
				time.Sleep(100 * time.Millisecond)
			}
		} else if role == FOLLOWER {
			select {
			case <-heartbeatEvent:
			case <-time.After(electionTimeout):
				handleElection()
			}
		} else {
			select {
			case <-leaderEvent:
				log.Println("We are leader...")
				numVotes = 0
				role = LEADER
				go initExternalServer()

			case <-time.After(electionTimeout):
				handleElection()

			case <-roleChange:
				continue
			}
		}
	}
}

func handleElection() {
	numVotes = 0
	alreadyVoted = false
	setElectionTimeout()
	electionTimeout = REELECTION_TIMEOUT*time.Millisecond - electionTimeout
	if alreadyVoted {
		return
	}

	role = CANDIDATE
	for peer := range peers {
		c := getConn(peer)
		go handleCandidateConn(c)
		sendRPC(RequestVote{
			LastLogIndex: lastLogIndex,
			LastLogTerm:  lastLogTerm,
		}, c)
	}
}

func handleCandidateConn(c *net.TCPConn) {
	rpc, err := getRPC(c)
	if err != nil {
		log.Fatalln(err)
	}

	if rpc.Type == "RequestVoteResponse" {
		body := castRPC[RequestVoteResponse](rpc)
		if rpc.Term > term {
			term = rpc.Term
			role = FOLLOWER
			roleChange <- struct{}{}
			return
		} else if body {
			numVotes++
			if numVotes >= (len(peers)+1)/2 {
				leaderEvent <- struct{}{}
				return
			}
		}
	}
}
