import os

MAX_FILE_SIZE = 400 * 1024 
EXCLUDE_DIRS = ['build', '.git', 'node_modules', '.idea','quic-go-custom']
EXCLUDE_EXTENSIONS = ['.exe', '.o', '.a', '.dll', '.png', '.jpg', '.pdf', '.zip', '.tar', '.gz', '.db', '.db-shm', '.db-wal', '.crt', '.key']
OUTPUT_PREFIX = "Cl_wins_"

def is_text_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            f.read(1024)
        return True
    except UnicodeDecodeError:
        return False

def generate_tree(startpath):
    tree_str = "==================================================\n"
    tree_str += "📂 PROJECT TREE STRUCTURE OUTPUT (CLIENT)\n"
    tree_str += "==================================================\n.\n"
    for root, dirs, files in os.walk(startpath):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        level = root.replace(startpath, '').count(os.sep)
        indent = '│   ' * (level - 1) + '├── ' if level > 0 else ''
        if root != startpath:
            tree_str += f"{indent}{os.path.basename(root)}\n"
        subindent = '│   ' * level + '├── '
        for f in files:
            tree_str += f"{subindent}{f}\n"
    tree_str += "\n\n"
    return tree_str

def compress_project(startpath):
    # Xóa file text nén cũ nếu có
    for f in os.listdir(startpath):
        if f.startswith(OUTPUT_PREFIX) and f.endswith(".txt"):
            os.remove(os.path.join(startpath, f))

    current_file_idx = 0
    current_out_path = os.path.join(startpath, f"{OUTPUT_PREFIX}{current_file_idx}.txt")
    out_file = open(current_out_path, 'w', encoding='utf-8')
    current_size = 0

    tree_content = generate_tree(startpath)
    out_file.write(tree_content)
    current_size += len(tree_content.encode('utf-8'))

    for root, dirs, files in os.walk(startpath):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext in EXCLUDE_EXTENSIONS or file.startswith(OUTPUT_PREFIX):
                continue
                
            filepath = os.path.join(root, file)
            if not os.path.exists(filepath) or not is_text_file(filepath):
                continue

            try:
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()
            except Exception as e:
                continue

            rel_path = os.path.relpath(filepath, startpath).replace('\\', '/')
            header = f"--- START FILE PATH: {rel_path} ---\n"
            footer = f"\n--- END OF FILE: {rel_path} ---\n\n"
            file_data = header + content + footer
            file_size = len(file_data.encode('utf-8'))

            if current_size + file_size > MAX_FILE_SIZE and current_size > 0:
                out_file.close()
                current_file_idx += 1
                current_out_path = os.path.join(startpath, f"{OUTPUT_PREFIX}{current_file_idx}.txt")
                out_file = open(current_out_path, 'w', encoding='utf-8')
                current_size = 0

            out_file.write(file_data)
            current_size += file_size

    out_file.close()
    print(f"Đã nén thành công mã nguồn thành {current_file_idx + 1} file(s) txt.")

if __name__ == "__main__":
    compress_project(os.getcwd())
