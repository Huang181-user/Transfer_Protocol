module zhiauth_client

go 1.25.0

require github.com/quic-go/quic-go v0.60.0

require (
	github.com/winfsp/cgofuse v1.6.0 // indirect
	golang.org/x/crypto v0.51.0 // indirect
	golang.org/x/net v0.55.0 // indirect
	golang.org/x/sys v0.45.0 // indirect
)

replace github.com/quic-go/quic-go => ../quic-go-custom