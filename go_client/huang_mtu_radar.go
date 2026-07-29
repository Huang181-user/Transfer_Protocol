package main

import (
	"fmt"
	"time"
)

// Helper bốc thời gian thực chính xác đến micro giây phục vụ Verbose Logging
func getRadarTS() string {
	return time.Now().Format("2006-01-02 15:04:05.000000")
}

// DiscoverBestHuangMTU thực thi thuật toán dò tìm leo thang phân đoạn của Kỹ sư Huang
// Khoảng dò từ 1000 đến 1500 bytes. Trả về mốc MTU tối ưu nhất tìm được.
func DiscoverBestHuangMTU(pingFunc func(targetSize int) bool) int {
	fmt.Println("==========================================================================")
	fmt.Printf("[%s] [MTU-RADAR] 📡 KÍCH HOẠT HỆ THỐNG TRINH SÁT MTU ĐỘNG [HOÀNG-HEURISTIC-ALGORITHM]\n", getRadarTS())
	fmt.Println("==========================================================================")
	fmt.Printf("[%s] [INFO] Ghi nhận hạ tầng Router VNPT: Giới hạn trần vật lý tối đa locked mạng WAN = 1500 bytes.\n", getRadarTS())

	// Bước 1: Thử nghiệm mốc kịch trần 1500 ngay từ đầu
	fmt.Printf("[%s] [MTU-RADAR][TRẦN-VẬT-LÝ] -> Đang phóng gói tin trinh sát kích thước cực đại: 1500 bytes...\n", getRadarTS())
	if pingFunc(1500) {
		fmt.Printf("[%s] [SUCCESS][MTU-RADAR] 🎉 Tuyệt vời! Đường truyền thông suốt hoàn hảo ở mốc cực đại 1500 bytes! Không cần hạ trần.\n", getRadarTS())
		return 1500
	}
	fmt.Printf("[%s] [WARNING][MTU-RADAR] 💥 Mốc 1500 bytes tịt ngòi! Hạ tầng dính nghẽn mạch hoặc bóp gói. Kích hoạt chia đôi phân đoạn...\n", getRadarTS())

	currentUpper := 1500
	currentLower := 1000

	// Vòng lặp chia đôi tìm mốc chạm chân đế
	for {
		if currentUpper-currentLower <= 1 {
			fmt.Printf("[%s] [MTU-RADAR][SÀN-TỐI-THIỂU] 🚨 Cảnh báo! Đã chia nhỏ phân đoạn tới mốc %d nhưng vẫn không thông mạch!\n", getRadarTS(), currentUpper)
			fmt.Printf("[%s] [RESULT-MTU] -> Ép cấu hình hạ tầng về mốc sàn an toàn tuyệt đối: 1000 bytes.\n", getRadarTS())
			return 1000
		}

		distance := (currentUpper - currentLower) / 2
		mid := currentLower + distance

		fmt.Printf("[%s] [MTU-RADAR][CHIA-ĐÔI] -> Khoảng cách hiện tại: %d | Thử nghiệm mốc trung vị: %d bytes...\n", getRadarTS(), distance*2, mid)

		if pingFunc(mid) {
			fmt.Printf("[%s] [SUCCESS][MTU-RADAR] 🎯 Mốc trung vị %d bytes PHẢN HỒI NGON LÀNH! Bắt đầu chiến dịch leo thang tìm điểm gãy...\n", getRadarTS(), mid)
			lastSuccess := mid

			// ----------------------------------------------------------------------
			// GIAI ĐOẠN LEO THANG 1: Tiến công theo hàng TRĂM (100)
			// ----------------------------------------------------------------------
			fmt.Printf("[%s] [MTU-RADAR][LEO-THANG-1] 📈 Kích nổ tiến trình quét theo hàng TRĂM (+100)...\n", getRadarTS())
			for val := lastSuccess + 100; val < currentUpper; val += 100 {
				fmt.Printf("[%s] [DEBUG][LEO-THANG-1] -> Thử nghiệm nấc thang hàng trăm: %d bytes...\n", getRadarTS(), val)
				if pingFunc(val) {
					fmt.Printf("[%s] [SUCCESS][LEO-THANG-1] -> Mốc %d bytes OK.\n", getRadarTS(), val)
					lastSuccess = val
				} else {
					fmt.Printf("[%s] [BURST][LEO-THANG-1] -> Mốc %d bytes BỊ CHẶN! Khóa vùng giới hạn trên mới = %d.\n", getRadarTS(), val, val)
					currentUpper = val
					break
				}
			}

			// ----------------------------------------------------------------------
			// GIAI ĐOẠN LEO THANG 2: Tiến công theo hàng CHỤC (10)
			// ----------------------------------------------------------------------
			fmt.Printf("[%s] [MTU-RADAR][LEO-THANG-2] 📈 Kích nổ tiến trình quét theo hàng CHỤC (+10) từ mốc %d...\n", getRadarTS(), lastSuccess)
			for val := lastSuccess + 10; val < currentUpper; val += 10 {
				fmt.Printf("[%s] [DEBUG][LEO-THANG-2] -> Thử nghiệm nấc thang hàng chục: %d bytes...\n", getRadarTS(), val)
				if pingFunc(val) {
					fmt.Printf("[%s] [SUCCESS][LEO-THANG-2] -> Mốc %d bytes OK.\n", getRadarTS(), val)
					lastSuccess = val
				} else {
					fmt.Printf("[%s] [BURST][LEO-THANG-2] -> Mốc %d bytes BỊ CHẶN! Khóa vùng giới hạn trên mới = %d.\n", getRadarTS(), val, val)
					currentUpper = val
					break
				}
			}

			// ----------------------------------------------------------------------
			// GIAI ĐOẠN LEO THANG 3: Tấn công loạt đạn cuối theo hàng ĐƠN VỊ (1)
			// ----------------------------------------------------------------------
			fmt.Printf("[%s] [MTU-RADAR][LEO-THANG-3] 📈 Kích nổ tiến trình quét tinh chỉnh hàng ĐƠN VỊ (+1) từ mốc %d...\n", getRadarTS(), lastSuccess)
			for val := lastSuccess + 1; val < currentUpper; val++ {
				fmt.Printf("[%s] [DEBUG][LEO-THANG-3] -> Thử nghiệm nấc thang đơn vị cực hạn: %d bytes...\n", getRadarTS(), val)
				if pingFunc(val) {
					fmt.Printf("[%s] [SUCCESS][LEO-THANG-3] -> Mốc %d bytes OK.\n", getRadarTS(), val)
					lastSuccess = val
				} else {
					fmt.Printf("[%s] [BURST][LEO-THANG-3] -> Mốc %d bytes CHÍNH THỨC SẬP BẪY PHÂN MẢNH!\n", getRadarTS(), val)
					break
				}
			}

			fmt.Println("==========================================================================")
			fmt.Printf("[%s] [CHẾ ĐỘ TỰ ĐỘNG THÍCH ỨNG] 🏆 THUẬT TOÁN KẾT THÚC! ĐÃ TÌM RA ĐỈNH MTU TỐI ƯU: [%d bytes]\n", getRadarTS(), lastSuccess)
			fmt.Println("==========================================================================")
			return lastSuccess

		} else {
			fmt.Printf("[%s] [BURST][CHIA-ĐÔI] -> Gói %d bytes bị rớt dọc đường. Co cụm giới hạn trên về %d bytes và lặp lại.\n", getRadarTS(), mid, mid)
			currentUpper = mid
		}
	}
}
