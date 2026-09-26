import os
import subprocess
import time

# --- CẤU HÌNH THAM SỐ TỐI ƯU ---
MAX_LINES_PER_FILE = 3000  # Giới hạn dòng mỗi file nén
IGNORE_DIRS = {
    'build', '.git', '__pycache__', 'DerivedData', 
    '.swiftpm', 'SourcePackages', 'checkouts', 'repositories',
    'Pods', 'xcshareddata', 'xcuserdata'
}
VALID_EXTENSIONS = {
    '.swift', '.yml', '.plist', '.h', '.cpp', '.c', 
    '.json', '.mod', '.sum', '.txt', '.example', '.md'
}

PREFIX = "ios_client_"

def get_realtime_ts():
    return time.strftime("%Y-%m-%d %H:%M:%S")

def log_info(msg):
    print(f"[{get_realtime_ts()}] [INFO] ℹ️ {msg}")

def log_success(msg):
    print(f"[{get_realtime_ts()}] [SUCCESS] 🎉 {msg}")

def log_debug(msg):
    print(f"[{get_realtime_ts()}] [DEBUG] 🔍 {msg}")

def run_tree_command():
    log_debug("Đang quét sơ đồ cây thư mục dự án iOS...")
    try:
        output = subprocess.check_output(
            ['tree', '-I', 'DerivedData|.git|.swiftpm|SourcePackages|build'], 
            text=True
        )
        log_success("Thu thập sơ đồ cây thư mục thành công!")
        return output
    except Exception as e:
        log_info(f"Không thể chạy lệnh tree (Dùng danh sách fallback): {e}")
        return "Tree command not available.\n"

def main():
    log_info(f"Khởi động hệ thống gom mã nguồn iOS Client v2.0 (Prefix: {PREFIX})")
    
    files_to_pack = []
    
    # Ưu tiên các file cấu hình quan trọng ở gốc
    priority_files = ["project.yml", "CMakeLists.txt"]
    for pf in priority_files:
        if os.path.exists(pf):
            log_debug(f"Phát hiện file cấu hình ưu tiên: {pf}")
            files_to_pack.append(pf)

    log_debug("Bắt đầu quét toàn bộ thư mục dự án iOS...")
    for root, dirs, files in os.walk('.'):
        # Bỏ qua các thư mục rác build Xcode/SPM
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS and not d.endswith('.xcodeproj') and not d.endswith('.xcworkspace')]
        
        for file in files:
            file_path = os.path.relpath(os.path.join(root, file), '.')
            
            # Chặn file script hiện tại và các file ẩn/temp
            if file == "compress_code.py" or file.startswith('.') or file.startswith('ios_client_'):
                continue
                
            ext = os.path.splitext(file)[1]
            if ext in VALID_EXTENSIONS or file in priority_files:
                if file_path not in files_to_pack:
                    files_to_pack.append(file_path)

    log_info(f"Tổng hợp chiến trường: Phát hiện [{len(files_to_pack)}] file mã nguồn iOS hợp lệ cần đóng gói.")

    file_index = 0
    current_lines = []
    
    tree_structure = run_tree_command()
    current_lines.append("==================================================\n")
    current_lines.append(f"📂 PROJECT TREE STRUCTURE OUTPUT (ZHIAUTH iOS CLIENT)\n")
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
            log_info(f"Đạt giới hạn trần dòng. Đang xuất xưởng container: {output_name}")
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
        log_info(f"Đang đóng gói container cuối cùng: {output_name}")
        with open(output_name, 'w', encoding='utf-8') as out_f:
            out_f.writelines(current_lines)
        log_success(f"Đóng gói hoàn tất: {output_name} ({len(current_lines)} dòng)")

    fmt_ts = get_realtime_ts()
    print(f"\n==========================================================================")
    print(f"🏆 PIPELINE HOÀN THÀNH MỸ MÃN LÚC [{fmt_ts}]")
    print(f"👉 Mã nguồn iOS đã gom sạch sẽ từ {PREFIX}0.txt đến {PREFIX}{file_index}.txt")
    print(f"==========================================================================")

if __name__ == "__main__":
    main()