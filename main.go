package main

import (
	"net/http"
)

func main() {
	http.HandleFunc("/", getRoot)
	err := http.ListenAndServe(":6800", nil)
	if err != nil {
		panic("Error listening on :6800")
	}
}

func getRoot(w http.ResponseWriter, r *http.Request) {
	w.WriteHeader(http.StatusBadGateway)
}
