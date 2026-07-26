package protocol

import (
	"fmt"
	"sync/atomic"
	"time"
)

type PacketType uint8

const (
	PacketTypeInitial PacketType = 1 + iota
	PacketTypeRetry
	PacketTypeHandshake
	PacketType0RTT
)

func (t PacketType) String() string {
	switch t {
	case PacketTypeInitial:
		return "Initial"
	case PacketTypeRetry:
		return "Retry"
	case PacketTypeHandshake:
		return "Handshake"
	case PacketType0RTT:
		return "0-RTT Protected"
	default:
		return fmt.Sprintf("unknown packet type: %d", t)
	}
}

type ECN uint8

const (
	ECNUnsupported ECN = iota
	ECNNon             // 00
	ECT1               // 01
	ECT0               // 10
	ECNCE              // 11
)

func ParseECNHeaderBits(bits byte) ECN {
	switch bits {
	case 0:
		return ECNNon
	case 0b00000010:
		return ECT0
	case 0b00000001:
		return ECT1
	case 0b00000011:
		return ECNCE
	default:
		panic("invalid ECN bits")
	}
}

func (e ECN) ToHeaderBits() byte {
	switch e {
	case ECNNon:
		return 0
	case ECT0:
		return 0b00000010
	case ECT1:
		return 0b00000001
	case ECNCE:
		return 0b00000011
	default:
		panic("ECN unsupported")
	}
}

func (e ECN) String() string {
	switch e {
	case ECNUnsupported:
		return "ECN unsupported"
	case ECNNon:
		return "Not-ECT"
	case ECT1:
		return "ECT(1)"
	case ECT0:
		return "ECT(0)"
	case ECNCE:
		return "CE"
	default:
		return fmt.Sprintf("invalid ECN value: %d", e)
	}
}

type ByteCount int64
type AtomicByteCount atomic.Int64

const MaxByteCount = ByteCount(1<<62 - 1)
const InvalidByteCount ByteCount = -1

// ==========================================================================
// VÁ CHUẨN ĐẾT: Trả về đúng danh phận định nghĩa kiểu dữ liệu (type)
// ==========================================================================
type StatelessResetToken [16]byte

const MaxPacketBufferSize = 1452
const MaxLargePacketBufferSize = 20 * 1024

// ==========================================================================
// 👑 ĐỘ CHẠM SÀN CLIENT: Hạ mốc sàn gói Initial xuống 1000 bytes đập tan hố đen 4G
// ==========================================================================
const MinInitialPacketSize = 1000

const MinUnknownVersionPacketSize = MinInitialPacketSize
const MinStatelessResetSize = 1 + 20 + 4 + 1 + 16
const MinReceivedStatelessResetSize = 5 + 16
const MinConnectionIDLenInitial = 8
const DefaultAckDelayExponent = 3
const DefaultActiveConnectionIDLimit = 2
const MaxAckDelayExponent = 20
const DefaultMaxAckDelay = 25 * time.Millisecond
const MaxMaxAckDelay = (1<<14 - 1) * time.Millisecond
const MaxConnIDLen = 20
const InvalidPacketLimitAES = 1 << 52
const InvalidPacketLimitChaCha = 1 << 36
