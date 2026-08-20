function Log-Realtime {
    param([string]$Message, [string]$Color = "Cyan")
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    Write-Host "[$ts] $Message" -ForegroundColor $Color
}

$startTime = [System.Diagnostics.Stopwatch]::StartNew()
Log-Realtime "[START] BAT DAU TIEN TRINH BIEN DICH CLIENT HYBRID (GO + C++)..." "Yellow"

# 1. Kiem tra moi truong
Log-Realtime "[CHECK] Kiem tra moi truong bien dich GCC va Go..." "Cyan"
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Log-Realtime "[FATAL] Khong tim thay 'g++'. Vui long kiem tra PATH!" "Red"
    exit 1
}
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    Log-Realtime "[FATAL] Khong tim thay 'go'. Vui long kiem tra moi truong Go!" "Red"
    exit 1
}
Log-Realtime "[OK] Moi truong bien dich hop le." "Green"

# 2. Thu muc build
$buildDir = "build"
if (Test-Path $buildDir) {
    Log-Realtime "[CLEAN] Dang don dep thu muc build cu '$buildDir'..." "Yellow"
    Remove-Item -Path "$buildDir\*" -Recurse -Force -ErrorAction SilentlyContinue
} else {
    Log-Realtime "[MKDIR] Tao thu muc dau ra '$buildDir'..." "Cyan"
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# 3. Quet tat ca thu muc Include noi bo
Log-Realtime "[SEARCH] Dang thu gom tat ca duong dan Include..." "Cyan"
$rawIncDirs = @("src", "src/bridge", "src/rpc_client")

Get-ChildItem -Path "." -Recurse -Directory | Where-Object { 
    $_.FullName -notmatch '\\(build|\.git|go_client)\b' 
} | ForEach-Object {
    $rawIncDirs += $_.FullName
}

$gppIncArgs = @()
foreach ($d in ($rawIncDirs | Select-Object -Unique)) {
    $gppIncArgs += "-I$d"
}

# 4. Compile C++ sang Object Files (.o) - CHỈ BIÊN DỊCH BỘ LÕI MỚI, BỎ QUA VFS_CLIENT.CPP CŨ
Log-Realtime "[PHASE 1] Dang bien dich cac file C/C++ active sang Object Files (.o)..." "Magenta"
$cppFiles = Get-ChildItem -Path "src" -Recurse -Include *.cpp, *.c | Where-Object {
    $_.Name -ne "vfs_client.cpp"
}

if ($cppFiles.Count -eq 0) {
    Log-Realtime "[FATAL] Khong tim thay file nguon C/C++ trong src/!" "Red"
    exit 1
}

foreach ($file in $cppFiles) {
    $objFile = "$buildDir\$($file.BaseName).o"
    Log-Realtime " |-- [COMPILE C++] $($file.Name) -> $objFile" "Gray"
    
    $argsList = $gppIncArgs + @("-c", $file.FullName, "-std=c++17", "-O3", "-static", "-o", $objFile)
    & g++ @argsList
    
    if ($LASTEXITCODE -ne 0) {
        Log-Realtime "[FATAL] Loi bien dich C++ tai file: $($file.Name)" "Red"
        exit 1
    }
}

# 5. Pack Static Library
$libPath = "$buildDir/libzhiauth_client_core.a"
Log-Realtime "[PHASE 2] Dang dong goi thu vien tinh: $libPath ..." "Magenta"
$oFiles = Get-ChildItem -Path $buildDir -Filter "*.o" | ForEach-Object { $_.FullName }
& ar rcs $libPath $oFiles

if ($LASTEXITCODE -ne 0) {
    Log-Realtime "[FATAL] Dong goi static library that bai!" "Red"
    exit 1
}
Log-Realtime "[OK] Dong goi $libPath thanh cong." "Green"

# 6. Build Go Client với CGO
Log-Realtime "[PHASE 3] Dang kich hoat CGO va bien dich Go Client..." "Magenta"
$env:CGO_ENABLED = "1"

Set-Location "go_client"
Log-Realtime " |-- [GO BUILD] Dang lien ket CGO voi Static Library C++..." "Gray"

& go build -ldflags="-s -w -extldflags '-static'" -o "../$buildDir/huang_client.exe" .
$goExitCode = $LASTEXITCODE
Set-Location ".."

if ($goExitCode -ne 0) {
    Log-Realtime "[FATAL] Bien dich Go Client that bai!" "Red"
    exit 1
}

$startTime.Stop()
$elapsed = $startTime.Elapsed.TotalSeconds.ToString("F2")

Log-Realtime "==========================================================================" "Yellow"
Log-Realtime "[SUCCESS] BUILD THANH CONG RUC RO TRONG $elapsed GIAY!" "Green"
Log-Realtime "[OUTPUT] File thuc thi tai: build/huang_client.exe" "Cyan"
Log-Realtime "==========================================================================" "Yellow"
