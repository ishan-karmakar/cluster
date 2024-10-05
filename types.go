package main

import "encoding/json"

type RPC struct {
	Type string
	Body json.RawMessage
}

type RequestVote struct {
	CurrentTerm  int
	LastLogIndex int
	LastLogTerm  int
	Candidate    string
}
