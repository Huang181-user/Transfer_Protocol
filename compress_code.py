import os
import subprocess
import time

# --- CẤU HÌNH DÀNH RIÊNG CHO ANDROID PROJECT ---
MAX_LINES_PER_FILE = 3000
IGNORE_DIRS = {
    'build', '.gradle', '.idea', '.git', '__pycache__', 
    'captures', '.externalNativeBuild', '.cxx', 'app/build'
}
VALID_EXTENSIONS = {
    '.kt', '.java', '.xml', '.gradle', '.kts', 
    '.json', '.properties', '.pro', '.cpp', '.h', '.c'
}
PREFIX = "android_"

def get_realtime_ts():
    return time.strftime("%Y-%m-%d %H:%M:%S")

def log_info(msg):
    print(f"[{get_realtime_ts()}] [INFO] ℹ️ {msg}")

def log_success(msg):
    print(f"[{get_realtime_ts()}] [SUCCESS] 🎉 {msg}")

def log_debug(msg):
    print(f"[{get_realtime_ts()}] [DEBUG] 🔍 {msg}")

def run_tree_command():
    log_debug("Đang quét cấu trúc thư mục Android Project...")
    try:
        output = subprocess.check_output(
            ['tree', '-I', 'build|.gradle|.idea|.cxx|captures'], 
            text=True
        )
        log_success("Thu thập sơ đồ cây Android thành công!")
        return output
    except Exception as e:
        log_info(f"Không thể chạy lệnh tree: {e}")
        return "Tree command not available.\n"

def main():
    log_info("Khởi động hệ thống gom mã nguồn ANDROID CLIENT v2.0")
    
    files_to_pack = []
    
    # Ưu tiên các file cốt lõi
    priority_files = ["AndroidManifest.xml", "build.gradle", "build.gradle.kts"]
    
    log_debug("Bắt đầu quét sâu dự án Android...")
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
        
        for file in files:
            file_path = os.path.relpath(os.path.join(root, file), '.')
            
            if file == "compress_android_code.py" or file.startswith('.'):
                continue
                
            ext = os.path.splitext(file)[1]
            if ext in VALID_EXTENSIONS or file in priority_files:
                if file_path not in files_to_pack:
                    files_to_pack.append(file_path)

    log_info(f"Phát hiện [{len(files_to_pack)}] file mã nguồn Android cần đóng gói.")

    file_index = 0
    current_lines = []
    
    tree_structure = run_tree_command()
    current_lines.append("==================================================\n")
    current_lines.append("📂 ANDROID PROJECT TREE STRUCTURE OUTPUT\n")
    current_lines.append("==================================================\n")
    current_lines.append(tree_structure)
    current_lines.append("\n\n")

    for file_path in files_to_pack:
        log_debug(f"Đang bóc tách file: {file_path}")
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.readlines()
        except Exception as e:
            log_info(f"Bỏ qua file {file_path}: {e}")
            continue

        file_header = f"--- START FILE PATH: {file_path} ---\n"
        file_footer = f"\n--- END OF FILE: {file_path} ---\n\n"
        
        estimated_lines = len(current_lines) + len(content) + 2
        
        if estimated_lines > MAX_LINES_PER_FILE and len(current_lines) > 5:
            output_name = f"{PREFIX}{file_index}.txt"
            log_info(f"Đạt giới hạn dòng. Đang xuất container: {output_name}")
            with open(output_name, 'w', encoding='utf-8') as out_f:
                out_f.writelines(current_lines)
            log_success(f"Ghi thành công: {output_name} ({len(current_lines)} dòng)")
            
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

    print(f"\n==========================================================================")
    print(f"🏆 PIPELINE HOÀN THÀNH MỸ MÃN LÚC [{get_realtime_ts()}]")
    print(f"👉 Mã nguồn Android được nén từ {PREFIX}0.txt đến {PREFIX}{file_index}.txt")
    print(f"==========================================================================")

if __name__ == "__main__":
    main()