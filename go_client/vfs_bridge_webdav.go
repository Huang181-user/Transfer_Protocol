//go:build windows
package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

func StartWindowsWebDavBridge(localPort, remoteBase string, tunnel *QuicTunnel, driveLetter string, protocol string) {
	log.Printf("[BRIDGE-%s] Khởi tạo Proxy WebDAV nội bộ trên Port: %s -> Vùng đích Ổ [%s:]", protocol, localPort, driveLetter)
	
	xmlEscape := func(s string) string {
		return strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", "\"", "&quot;", "'", "&apos;").Replace(s)
	}

	encodeURI := func(p string) string {
		segments := strings.Split(p, "/")
		for i, seg := range segments {
			segments[i] = url.PathEscape(seg)
		}
		return strings.Join(segments, "/")
	}

	getVfsPath := func(reqPath string) string {
		p := strings.ReplaceAll(reqPath, "\\", "/")
		if strings.HasPrefix(strings.ToLower(p), "/davwwwroot") {
			p = p[len("/davwwwroot"):]
		}
		if p == "" || !strings.HasPrefix(p, "/") {
			p = "/" + p
		}
		return strings.TrimSuffix(remoteBase, "/") + p
	}

	buildDavHref := func(reqPath, name string, isDir bool) string {
		cleanReq := strings.TrimSuffix(reqPath, "/")
		full := cleanReq + "/" + url.PathEscape(name)
		if isDir && !strings.HasSuffix(full, "/") {
			full += "/"
		}
		return xmlEscape(full)
	}

	isWindowsSystemProbe := func(path string) bool {
		l := strings.ToLower(path)
		return strings.Contains(l, "desktop.ini") || 
		       strings.Contains(l, "autorun.inf") || 
		       strings.Contains(l, "thumbs.db") || 
		       strings.Contains(l, "folder.jpg") ||
		       strings.Contains(l, "folder.ico")
	}

	go func() {
		mux := http.NewServeMux()
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			ts := time.Now().Format("2006-01-02 15:04:05.000000")
			realPath := getVfsPath(r.URL.Path)

			if !isWindowsSystemProbe(r.URL.Path) {
				log.Printf("[%s] [WEBDAV-%s] 📥 HTTP %s %s -> Real VFS: %s", ts, protocol, r.Method, r.URL.Path, realPath)
			}
			
			w.Header().Set("Server", "ZhiAuth-WebDAV")
			w.Header().Set("MS-Author-Via", "DAV")
			w.Header().Set("Accept-Ranges", "bytes")
			w.Header().Set("Cache-Control", "no-cache, no-store, must-revalidate")
			w.Header().Set("Pragma", "no-cache")
			w.Header().Set("Expires", "0")
			
			if r.Method == "OPTIONS" || r.Method == "LOCK" || r.Method == "UNLOCK" {
				w.Header().Set("Allow", "OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, COPY, MOVE, MKCOL, PROPFIND, PROPPATCH, LOCK, UNLOCK")
				w.Header().Set("DAV", "1, 2")
				if r.Method == "LOCK" {
					w.Header().Set("Content-Type", "application/xml; charset=utf-8")
					w.WriteHeader(http.StatusOK)
					w.Write([]byte(`<?xml version="1.0" encoding="utf-8"?><d:prop xmlns:d="DAV:"><d:lockdiscovery><d:activelock><d:locktype><d:write/></d:locktype><d:lockscope><d:exclusive/></d:lockscope><d:depth>0</d:depth><d:owner><d:href>ZhiAuth</d:href></d:owner><d:timeout>Second-3600</d:timeout><d:locktoken><d:href>opaquelocktoken:zhiauth-lock-token</d:href></d:locktoken></d:activelock></d:lockdiscovery></d:prop>`))
					return
				}
				w.WriteHeader(http.StatusOK)
				return
			}

			callVfsWithOffset := func(opcode byte, targetRealPath string, offset uint64, reqLen uint32, dataPayload []byte) ([]byte, error) {
				if protocol == "KCP" {
					return SendRpcVfs(opcode, targetRealPath, offset, reqLen, dataPayload)
				}
				rawPacket := BuildVfsPacket(opcode, targetRealPath, offset, reqLen, dataPayload)
				return tunnel.SendFsCommandRaw(rawPacket)
			}

			if r.Method == "PROPPATCH" {
				w.Header().Set("Content-Type", "application/xml; charset=utf-8")
				w.WriteHeader(http.StatusMultiStatus)
				w.Write([]byte(fmt.Sprintf(`<?xml version="1.0" encoding="utf-8"?><d:multistatus xmlns:d="DAV:"><d:response><d:href>%s</d:href><d:propstat><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>`, xmlEscape(encodeURI(r.URL.Path)))))
				return
			}

			if r.Method == "PROPFIND" {
				statPayload, err := callVfsWithOffset(OP_STAT, realPath, 0, 0, nil)
				if err != nil || len(statPayload) < 9 {
					if !isWindowsSystemProbe(r.URL.Path) {
						log.Printf("[%s] [WEBDAV-404] ❌ VFS STAT lỗi cho Path: %s", ts, realPath)
					}
					w.WriteHeader(http.StatusNotFound)
					return
				}

				w.Header().Set("Content-Type", "application/xml; charset=utf-8")
				w.WriteHeader(http.StatusMultiStatus)
				
				depth := r.Header.Get("Depth")
				isRootFile := (statPayload[8] == 0)
				rootSize := binary.LittleEndian.Uint64(statPayload[0:8])

				reqHref := encodeURI(r.URL.Path)
				if !isRootFile && !strings.HasSuffix(reqHref, "/") {
					reqHref += "/"
				}

				rootPropNode := "<d:resourcetype><d:collection/></d:resourcetype>"
				if isRootFile {
					rootPropNode = fmt.Sprintf("<d:resourcetype/>\n        <d:getcontentlength>%d</d:getcontentlength>", rootSize)
				}

				xmlResponses := fmt.Sprintf(`  <d:response>
    <d:href>%s</d:href>
    <d:propstat>
      <d:prop>
        %s
        <d:getlastmodified>Sun, 19 Jul 2026 21:30:00 GMT</d:getlastmodified>
      </d:prop>
      <d:status>HTTP/1.1 200 OK</d:status>
    </d:propstat>
  </d:response>`, xmlEscape(reqHref), rootPropNode)

				if depth != "0" && !isRootFile {
					payload, err := callVfsWithOffset(OP_LIST, realPath, 0, 0, nil)
					if err == nil && len(payload) > 0 {
						tokens := strings.Split(string(payload), "|")
						for _, token := range tokens {
							if token == "" { continue }
							parts := strings.Split(token, ",")
							if len(parts) < 2 { continue }
							
							name, isDir := parts[0], parts[1] == "DIR"
							sizeStr := "0"
							if len(parts) >= 3 { sizeStr = parts[2] }
							
							safeName := xmlEscape(name)
							safeHref := buildDavHref(encodeURI(r.URL.Path), name, isDir)

							propNode := fmt.Sprintf("<d:resourcetype/>\n        <d:getcontentlength>%s</d:getcontentlength>", sizeStr)
							if isDir { propNode = "<d:resourcetype><d:collection/></d:resourcetype>" }

							xmlResponses += fmt.Sprintf(`
  <d:response>
    <d:href>%s</d:href>
    <d:propstat>
      <d:prop>
        %s
        <d:displayname>%s</d:displayname>
        <d:getlastmodified>Sun, 19 Jul 2026 21:30:00 GMT</d:getlastmodified>
      </d:prop>
      <d:status>HTTP/1.1 200 OK</d:status>
    </d:propstat>
  </d:response>`, safeHref, propNode, safeName)
						}
					}
				}
				w.Write([]byte(fmt.Sprintf(`<?xml version="1.0" encoding="utf-8" ?><d:multistatus xmlns:d="DAV:">%s</d:multistatus>`, xmlResponses)))
				return
			}

			if r.Method == "GET" || r.Method == "HEAD" {
				statPayload, err := callVfsWithOffset(OP_STAT, realPath, 0, 0, nil)
				if err != nil || len(statPayload) < 8 { w.WriteHeader(http.StatusNotFound); return }
				
				fileSize := binary.LittleEndian.Uint64(statPayload[0:8])
				rangeHeader := r.Header.Get("Range")
				var start, end uint64 = 0, fileSize - 1
				isPartial := false

				if rangeHeader != "" && strings.HasPrefix(rangeHeader, "bytes=") {
					parts := strings.Split(strings.TrimPrefix(rangeHeader, "bytes="), "-")
					if len(parts) == 2 {
						if parts[0] != "" { start, _ = strconv.ParseUint(parts[0], 10, 64) }
						if parts[1] != "" { 
							parsedEnd, _ := strconv.ParseUint(parts[1], 10, 64)
							if parsedEnd < end { end = parsedEnd }
						}
					}
					isPartial = true
				}

				if start > end || start >= fileSize {
					w.Header().Set("Content-Range", fmt.Sprintf("bytes */%d", fileSize))
					w.WriteHeader(http.StatusRequestedRangeNotSatisfiable)
					return
				}

				contentLength := end - start + 1
				w.Header().Set("Content-Type", "application/octet-stream")
				w.Header().Set("Content-Length", fmt.Sprintf("%d", contentLength))

				if isPartial {
					w.Header().Set("Content-Range", fmt.Sprintf("bytes %d-%d/%d", start, end, fileSize))
					w.WriteHeader(http.StatusPartialContent)
				} else {
					w.WriteHeader(http.StatusOK)
				}

				if r.Method == "HEAD" { return }

				log.Printf("[%s] [WEBDAV-STREAM-%s] 🎵 Streaming: %s | Range: %d-%d", ts, protocol, realPath, start, end)

				chunkSize := uint64(131072) 
				currOffset := start

				for currOffset <= end {
					sz := uint32(chunkSize)
					if currOffset+uint64(sz) > end+1 { sz = uint32(end - currOffset + 1) }

					chunk, err := callVfsWithOffset(OP_READ, realPath, currOffset, sz, nil)
					if err != nil || len(chunk) == 0 { break }

					w.Write(chunk)
					if flusher, ok := w.(http.Flusher); ok { flusher.Flush() }
					
					currOffset += uint64(len(chunk))
				}
				return
			}

			// 🔥 STREAMING CHUẨN XÁC NỐI TIẾP: Cập nhật ReadFull tránh nghẽn KCP
			if r.Method == "PUT" {
				log.Printf("[%s] [WEBDAV-UPLOAD-STREAM-%s] 📥 Ghi file Streaming: %s", ts, protocol, realPath)

				buf := make([]byte, 131072) // 128KB Chunk
				var currOffset uint64 = 0

				for {
					n, err := r.Body.Read(buf)
					if n > 0 {
						_, writeErr := callVfsWithOffset(OP_WRITE, realPath, currOffset, uint32(n), buf[:n])
						if writeErr != nil {
							log.Printf("[%s] [WEBDAV-UPLOAD-ERR] Lỗi ghi chunk offset %d: %v", ts, currOffset, writeErr)
							w.WriteHeader(http.StatusInternalServerError)
							return
						}
						currOffset += uint64(n)
					}
					if err == io.EOF || err == io.ErrUnexpectedEOF {
						break
					}
					if err != nil {
						log.Printf("[%s] [WEBDAV-UPLOAD-ERR] Lỗi đọc stream HTTP: %v", ts, err)
						w.WriteHeader(http.StatusInternalServerError)
						return
					}
				}

				if currOffset == 0 {
					callVfsWithOffset(OP_WRITE, realPath, 0, 0, []byte{})
				}

				w.WriteHeader(http.StatusCreated)
				return
			}

			if r.Method == "MKCOL" {
				_, err := callVfsWithOffset(OP_MKDIR, realPath, 0, 0, nil)
				if err != nil { w.WriteHeader(http.StatusInternalServerError); return }
				w.WriteHeader(http.StatusCreated)
				return
			}

			if r.Method == "DELETE" {
				_, err := callVfsWithOffset(OP_DELETE, realPath, 0, 0, nil)
				if err != nil { w.WriteHeader(http.StatusInternalServerError); return }
				w.WriteHeader(http.StatusOK)
				return
			}

			if r.Method == "MOVE" {
				dest := r.Header.Get("Destination")
				if idx := strings.Index(dest, "://"); idx != -1 {
					dest = dest[idx+3:]
					if idxSlash := strings.Index(dest, "/"); idxSlash != -1 { dest = dest[idxSlash:] }
				}
				
				dest, _ = url.PathUnescape(dest)
				realDestPath := getVfsPath(dest)
				_, err := callVfsWithOffset(OP_RENAME, realPath, 0, 0, []byte(realDestPath))
				if err != nil { w.WriteHeader(http.StatusInternalServerError); return }
				w.WriteHeader(http.StatusCreated)
				return
			}

			w.Header().Set("Content-Type", "text/plain; charset=utf-8")
			w.WriteHeader(http.StatusMethodNotAllowed)
		})
		http.ListenAndServe(":"+localPort, mux)
	}()

	time.Sleep(500 * time.Millisecond)
	
	exec.Command("net", "use", driveLetter+":", "/delete", "/y").Run()
	targetURL := fmt.Sprintf("http://127.0.0.1:%s/", localPort)
	
	cmd := exec.Command("net", "use", driveLetter+":", targetURL, "/persistent:no", "/y")
	if output, err := cmd.CombinedOutput(); err != nil {
		log.Printf("❌ [WINDOWS-OS-ERR-%s] Thất bại! Windows báo lỗi: %s", protocol, strings.TrimSpace(string(output)))
	} else {
		log.Printf("✅ [SUCCESS-%s] 🚀 Tuyệt vời! Windows đã gán ổ mạng [%s:]", protocol, driveLetter)
	}
}
