package main

import "encoding/json"

type RPC struct {
	Type string
	Term int
	Body json.RawMessage
}

type RequestVote struct {
	LastLogIndex int
	LastLogTerm  int
}

// If vote was granted or not
type RequestVoteResponse bool

type AppendEntries struct {
}
