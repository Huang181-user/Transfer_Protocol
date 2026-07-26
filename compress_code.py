import os
import subprocess
import time

# --- CẤU HÌNH THAM SỐ TỐI ƯU ---
MAX_LINES_PER_FILE = 3000  # Nới trần để giảm số lượng file
IGNORE_DIRS = {'build', 'quic-go-custom', '.git', '__pycache__'}
VALID_EXTENSIONS = {'.go', '.cpp', '.h', '.c', '.json', '.mod', '.sum', '.txt', '.example'} 

# 🌟 RADAR TỰ ĐỘNG NHẬN DIỆN MÔI TRƯỜNG
CURRENT_DIR_NAME = os.path.basename(os.path.abspath(os.getcwd())).lower()
PREFIX = "client_" if "client" in CURRENT_DIR_NAME else "server_"

def get_realtime_ts():
    return time.strftime("%Y-%m-%d %H:%M:%S")

def log_info(msg):
    print(f"[{get_realtime_ts()}] [INFO] ℹ️ {msg}")

def log_success(msg):
    print(f"[{get_realtime_ts()}] [SUCCESS] 🎉 {msg}")

def log_debug(msg):
    print(f"[{get_realtime_ts()}] [DEBUG] 🔍 {msg}")

def run_tree_command():
    log_debug("Đang kích nổ lệnh 'tree' để thu thập cấu trúc hạ tầng...")
    try:
        output = subprocess.check_output(['tree', '-I', 'build|quic-go-custom'], text=True)
        log_success("Thu thập sơ đồ cây thư mục thành công!")
        return output
    except Exception as e:
        log_info(f"Không thể chạy lệnh tree (Có thể chưa cài): {e}")
        return "Tree command not available.\n"

def main():
    log_info(f"Khởi động hệ thống gom mã nguồn tự động v2.0 (Chế độ: {PREFIX.upper().strip('_')})")
    
    files_to_pack = []
    
    if os.path.exists("CMakeLists.txt"):
        log_debug("Phát hiện cấu hình tủy sống CMakeLists.txt, đưa vào danh sách ưu tiên.")
        files_to_pack.append("CMakeLists.txt")

    log_debug("Bắt đầu POSIX API quét sâu toàn bộ thư mục gốc...")
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
        
        for file in files:
            file_path = os.path.relpath(os.path.join(root, file), '.')
            
            # Đã gỡ bỏ luật cấm file .txt, chỉ chặn chính nó và file ẩn
            if file == "compress_code.py" or file.startswith('.'):
                continue
                
            ext = os.path.splitext(file)[1]
            if ext in VALID_EXTENSIONS or file == "CMakeLists.txt":
                if file_path not in files_to_pack:
                    files_to_pack.append(file_path)

    log_info(f"Tổng hợp chiến trường: Phát hiện [{len(files_to_pack)}] file mã nguồn hợp lệ cần đóng gói.")

    file_index = 0
    current_lines = []
    
    tree_structure = run_tree_command()
    current_lines.append("==================================================\n")
    current_lines.append(f"📂 PROJECT TREE STRUCTURE OUTPUT ({PREFIX.upper().strip('_')})\n")
    current_lines.append("==================================================\n")
    current_lines.append(tree_structure)
    current_lines.append("\n\n")

    for file_path in files_to_pack:
        log_debug(f"Đang bóc tách dữ liệu tệp tin: {file_path}")
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.readlines()
        except Exception as e:
            log_info(f"Bỏ qua file {file_path} do lỗi đọc: {e}")
            continue

        file_header = f"--- START FILE PATH: {file_path} ---\n"
        file_footer = f"\n--- END OF FILE: {file_path} ---\n\n"
        
        estimated_lines = len(current_lines) + len(content) + 2
        
        if estimated_lines > MAX_LINES_PER_FILE and len(current_lines) > 5:
            output_name = f"{PREFIX}{file_index}.txt"
            log_info(f"Đạt giới hạn trần dòng đè. Đang xuất xưởng khối container: {output_name}")
            with open(output_name, 'w', encoding='utf-8') as out_f:
                out_f.writelines(current_lines)
            log_success(f"Ghi thành công tệp tin: {output_name} ({len(current_lines)} dòng)")
            
            file_index += 1
            current_lines = []

        current_lines.append(file_header)
        current_lines.extend(content)
        current_lines.append(file_footer)

    if current_lines:
        output_name = f"{PREFIX}{file_index}.txt"
        log_info(f"Đang đóng gói khối container cuối cùng: {output_name}")
        with open(output_name, 'w', encoding='utf-8') as out_f:
            out_f.writelines(current_lines)
        log_success(f"Hệ thống Live đóng cấu trúc hoàn tất: {output_name} ({len(current_lines)} dòng)")

    fmt_ts = get_realtime_ts()
    print(f"\n==========================================================================")
    print(f"🏆 PIPELINE HOÀN THÀNH MỸ MÃN LÚC [{fmt_ts}]")
    print(f"👉 Toàn bộ mã nguồn đã được nén gọn gàng từ {PREFIX}0.txt đến {PREFIX}{file_index}.txt")
    print(f"==========================================================================")

if __name__ == "__main__":
    main()
