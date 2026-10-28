param([switch]$NoLaunch, [switch]$PythonBridge, [int]$PythonBridgePort = 40242)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$nasm = 'C:\Users\ashwi\Downloads\nasm-3.02rc13-win64\nasm-3.02rc13\nasm.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'
if (-not (Test-Path $nasm)) { $n = Get-Command nasm -ErrorAction SilentlyContinue; if ($n) { $nasm = $n.Source } }
if (-not (Test-Path $nasm)) { throw 'NASM is required.' }
if (-not (Test-Path $qemu)) { throw 'QEMU is required.' }
$bootBin = Join-Path $root 'boot32.bin'
$kernelBin = Join-Path $root 'kernel32.bin'
$image = Join-Path $root 'aarambh-v04-pm32-fs.img'
$py = Get-Command py -ErrorAction SilentlyContinue
if (-not $py) { throw 'Python 3 is required to initialize/check the disposable virtual image.' }
$micropyDir = Join-Path $root 'micropy32'
$micropyBin = Join-Path $micropyDir 'micropy32.bin'
$env:PATH = "C:\msys64\mingw32\bin;C:\msys64\usr\bin;$env:PATH"
& "C:\msys64\usr\bin\make.exe" -C $micropyDir
if ($LASTEXITCODE -ne 0) { throw 'Failed building native MicroPython runtime.' }
& $nasm -f bin (Join-Path $root 'bootloader\boot.asm') -o $bootBin
if ($LASTEXITCODE -ne 0) { throw 'NASM failed assembling BIOS boot sector.' }
& $nasm -I "$root\kernel\" -f bin (Join-Path $root 'kernel\kernel32.asm') -o $kernelBin
if ($LASTEXITCODE -ne 0) { throw 'NASM failed assembling protected-mode kernel.' }
$boot = [IO.File]::ReadAllBytes($bootBin)
$kernel = [IO.File]::ReadAllBytes($kernelBin)
if ($boot.Length -ne 512 -or $boot[510] -ne 0x55 -or $boot[511] -ne 0xAA) { throw 'Invalid BIOS boot sector.' }
if ($kernel.Length -gt 32256) { throw "Kernel is $($kernel.Length) bytes; loader maximum is 32256." }
if (-not (Test-Path $image)) { & $py.Source -3 (Join-Path $root 'format_aarfs.py') --create $image; if ($LASTEXITCODE -ne 0) { throw 'Could not initialize new virtual AarFS disk.' } }
else { & $py.Source -3 (Join-Path $root 'format_aarfs.py') --check $image; if ($LASTEXITCODE -ne 0) { throw 'Existing virtual AarFS disk check failed; it was not modified.' } }
$fs = [IO.File]::Open($image, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
  $fs.Position = 0; $fs.Write($boot,0,446)
  $fs.Position = 510; $fs.Write($boot,510,2)
  $fs.Position = 512; $fs.Write($kernel,0,$kernel.Length)
  if (Test-Path $micropyBin) {
    $mpy = [IO.File]::ReadAllBytes($micropyBin)
    $fs.Position = 132000 * 512
    $fs.Write($mpy, 0, $mpy.Length)
    Write-Host "Native MicroPython runtime: $($mpy.Length) bytes installed at LBA 132000."
  }
} finally { $fs.Dispose() }
Write-Host "Built protected-mode AarFS image: $image"
Write-Host "Kernel size: $($kernel.Length) bytes; BIOS loader limit: 32256 bytes."
Write-Host 'Only this separate 128 MiB virtual disk is attached to QEMU.'
if (-not $NoLaunch) {
  if ($PythonBridge) {
    if ($PythonBridgePort -lt 1 -or $PythonBridgePort -gt 65535) { throw 'PythonBridgePort must be in 1..65535.' }
    $debugLog = Join-Path $root 'pm32-debug.log'
    Write-Host "COM1 kernel diagnostics: $debugLog"
    Write-Host "COM2 Python bridge: QEMU listens on 127.0.0.1:$PythonBridgePort (loopback only)."
    Write-Host 'Start host_python_bridge.py with the same --port; it connects to QEMU and retries.'
    & $qemu -drive "file=$image,format=raw,if=ide,index=0" -boot c -serial "file:$debugLog" -chardev "socket,id=pybridge,host=127.0.0.1,port=$PythonBridgePort,server=on,wait=off" -device 'isa-serial,chardev=pybridge,iobase=0x2f8,irq=3'
  } else {
    & $qemu -drive "file=$image,format=raw,if=ide,index=0" -boot c
  }
}
