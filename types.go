package main

import "encoding/json"

type RPC struct {
	Type string
	Body json.RawMessage
}

type RequestVote struct {
	Term         int
	LastLogIndex int
	LastLogTerm  int
}

// If vote was granted or not
type RequestVoteResponse struct {
	Term        int
	VoteGranted bool
}
