package main

import "net/http"

func initExternalServer() {
	http.HandleFunc("/", handleRoot)
	http.ListenAndServe(externalServer, nil)
}

func handleRoot(writer http.ResponseWriter, req *http.Request) {
	writer.Write([]byte("Hello World"))
}
