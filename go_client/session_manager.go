package main

import (
	"bufio"
	"log"
	"os"
	"strings"
)

func LoadDeviceSession(filepath string) (string, string, string, bool) {
	log.Printf("[DEBUG] [session_manager.go:LoadDeviceSession] -> Scanning session token container at: %s", filepath)
	file, err := os.Open(filepath)
	if err != nil { return "", "", "", false }
	defer file.Close()

	scanner := bufio.NewScanner(file)
	var lines []string
	for scanner.Scan() {
		lines = append(lines, strings.TrimSpace(scanner.Text()))
	}

	if len(lines) >= 3 {
		log.Printf("[SUCCESS] [session_manager.go:LoadDeviceSession] -> Identity capsule decoded. Account: [%s] | Target Mount Point: [%s]", lines[0], lines[2])
		return lines[0], lines[1], lines[2], true
	}
	return "", "", "", false
}

func SaveDeviceSession(filepath, user, pass, mountPath string) bool {
	log.Printf("[DEBUG] [session_manager.go:SaveDeviceSession] -> Committing 3-axis security identity tokens onto disk matrix.")
	file, err := os.Create(filepath)
	if err != nil {
		log.Printf("[ERROR] [session_manager.go:SaveDeviceSession] -> Local file mapping locked.")
		return false
	}
	defer file.Close()

	file.WriteString(user + "\n" + pass + "\n" + mountPath + "\n")
	log.Println("[SUCCESS] [session_manager.go:SaveDeviceSession] -> Persistent state committed safely.")
	return true
}
