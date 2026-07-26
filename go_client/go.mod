module zhiauth_client

go 1.25.0

require (
	github.com/hanwen/go-fuse/v2 v2.5.1
	github.com/quic-go/quic-go v0.60.0
)

require (
	golang.org/x/crypto v0.51.0 // indirect
	golang.org/x/net v0.55.0 // indirect
	golang.org/x/sys v0.45.0 // indirect
)

replace github.com/quic-go/quic-go => /home/huang/zhiauth_client/quic-go-custom
