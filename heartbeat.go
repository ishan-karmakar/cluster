package main

import (
	"log"
	"net"
	"time"
)

func checkHeartbeat() {
	setElectionTimeout()
	log.Println("Election timeout:", electionTimeout)
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
				alreadyVoted = false
				numVotes = 0
				log.Println("Election timeout expired")
				handleElection()
			}
		} else {
			select {
			case <-leaderEvent:
				numVotes = 0
				role = LEADER
				go initExternalServer()

			case <-time.After(electionTimeout):
				numVotes = 0
				alreadyVoted = false
				log.Println("Election timeout expired again...")
				handleElection()

			case <-roleChange:
				continue
			}
		}
	}
}

func handleElection() {
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
			log.Println("Received vote")
			numVotes++
			if numVotes >= len(peers)/2 {
				leaderEvent <- struct{}{}
				return
			}
		}
	}
}
