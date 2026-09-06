function Log-Realtime {
    param([string]$Message, [string]$Color = "Cyan")
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    Write-Host "[$ts] $Message" -ForegroundColor $Color
}

$startTime = [System.Diagnostics.Stopwatch]::StartNew()
Log-Realtime "[START] BAT DAU BIEN DICH PURE C++ CLIENT (FUSE API)..." "Yellow"

$buildDir = "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

# 🔥 Cập nhật mảng Include chuẩn hóa
$rawIncDirs = @("include", "src")
$gppIncArgs = @()
foreach ($d in ($rawIncDirs | Select-Object -Unique)) { $gppIncArgs += "-I`"$d`"" }

$searchRoots = @("C:\Program Files (x86)\WinFsp", "C:\Program Files\WinFsp", "C:\msys64")
$foundHeader = $null
foreach ($root in $searchRoots) {
    if (Test-Path $root) {
        $foundHeader = Get-ChildItem -Path $root -Filter "fuse.h" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundHeader) { break }
    }
}

if ($foundHeader) {
    $fuseIncDir = $foundHeader.DirectoryName
    $winfspIncDir = (Get-Item $fuseIncDir).Parent.FullName
    $gppIncArgs += "-I`"$winfspIncDir`""
    $gppIncArgs += "-I`"$fuseIncDir`""
    Log-Realtime "[WINFSP] Tim thay thuc muc Include: $winfspIncDir" "Green"
} else {
    Log-Realtime "[FATAL] Khong tim thay WinFSP!" "Red"
    exit 1
}

$foundLib = $null
foreach ($root in $searchRoots) {
    if (Test-Path $root) {
        $foundLib = Get-ChildItem -Path $root -Filter "winfsp-x64.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundLib) { break }
    }
}

$fuseLibArg = ""
if ($foundLib) {
    $fuseLibArg = "`"" + $foundLib.FullName + "`""
    Log-Realtime "[WINFSP] Tim thay thuc muc Library: $fuseLibArg" "Green"
} else {
    Log-Realtime "[FATAL] Khong tim thay winfsp-x64.dll!" "Red"
    exit 1
}

$libs = @("-L$buildDir", "-Llib", "-lws2_32", "-liphlpapi", "-lcredui", $fuseLibArg, "-lsodium", "-lmsquic", "-lwinmm")
$cppFiles = @(
    "src\client_main.cpp",
    "src\bridge\client_bridge.cpp",
    "src\bridge\win_auth.cpp",
    "src\common\logger.cpp",
    "src\rpc_client\crypto_box.cpp",
    "src\rpc_client\ikcp.c",
    "src\rpc_client\vfs_client.cpp",
    "src\rpc_quic\msquic_client.cpp",
    "src\system\sys_utils.cpp",
    "src\vfs\fuse_driver.cpp"
)

Log-Realtime "[COMPILE] Dang bien dich bang g++ (co ep xung luong)..." "Magenta"

$argsList = $gppIncArgs + $cppFiles + @(
    "-std=c++17", 
    "-O3", "-static", "-static-libgcc", "-static-libstdc++", 
    "-DQUIC_SAL_STUB", 
    "-D_FILE_OFFSET_BITS=64", 
    "-DFUSE_USE_VERSION=28", 
    "-o", "$buildDir\huang_client_win.exe"
) + $libs

& g++ @argsList

if ($LASTEXITCODE -ne 0) {
    Log-Realtime "[FATAL] Bien dich that bai!" "Red"
    exit 1
}

Copy-Item "lib\msquic.dll" -Destination "$buildDir\" -Force

$elapsed = $startTime.Elapsed.TotalSeconds.ToString("F2")
Log-Realtime "==========================================================================" "Yellow"
Log-Realtime "[SUCCESS] PURE C++ CLIENT (320KB LIMIT) DA SAN SANG TRONG $elapsed GIAY!" "Green"
Log-Realtime "==========================================================================" "Yellow"