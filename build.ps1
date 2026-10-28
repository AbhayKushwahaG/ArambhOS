$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$nasm = 'C:\Users\ashwi\Downloads\nasm-3.02rc13-win64\nasm-3.02rc13\nasm.exe'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'
if (-not (Test-Path $nasm)) { $n = Get-Command nasm -ErrorAction SilentlyContinue; if ($n) { $nasm = $n.Source } }
if (-not (Test-Path $nasm)) { throw "NASM not found. Edit `$nasm in build.ps1." }
$python = Get-Command py -ErrorAction SilentlyContinue
if ($python) { $pyExe = $python.Source; $pyArgs = @('-3') } else { $python = Get-Command python -ErrorAction SilentlyContinue; if (-not $python) { throw 'Python 3 is required to create the virtual disk image.' }; $pyExe = $python.Source; $pyArgs = @() }
$bootSrc = Join-Path $root 'bootloader\boot.asm'
$kernelSrc = Join-Path $root 'kernel\kernel.asm'
$bootBin = Join-Path $root 'boot.bin'
$kernelBin = Join-Path $root 'kernel.bin'
$image = Join-Path $root 'aarambh-v04-fat32-rw.img'
& $nasm -f bin $bootSrc -o $bootBin
if ($LASTEXITCODE -ne 0) { throw 'NASM failed assembling bootloader.' }
& $nasm -I "$root\kernel\" -f bin $kernelSrc -o $kernelBin
if ($LASTEXITCODE -ne 0) { throw 'NASM failed assembling kernel.' }
$boot = [IO.File]::ReadAllBytes($bootBin)
$kernel = [IO.File]::ReadAllBytes($kernelBin)
if ($boot.Length -ne 512) { throw "Boot sector is $($boot.Length) bytes, expected 512." }
if ($boot[510] -ne 0x55 -or $boot[511] -ne 0xAA) { throw 'Boot signature missing.' }
if ($kernel.Length -gt 32256) { throw "Kernel is $($kernel.Length) bytes; maximum is 32256." }
& $pyExe @pyArgs (Join-Path $root 'make_image.py') $image
if ($LASTEXITCODE -ne 0) { throw 'Virtual disk image creation failed.' }
$fs = [IO.File]::Open($image, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
  $fs.Position = 0; $fs.Write($boot, 0, 446)
  $fs.Position = 510; $fs.Write($boot, 510, 2)
  $fs.Position = 512; $fs.Write($kernel, 0, $kernel.Length)
} finally { $fs.Dispose() }
Write-Host "Built virtual disk: $image"
Write-Host 'This image boots Aarambh and preserves an existing valid FAT32 data volume across rebuilds.'
Write-Host 'FAT32 supports growing root directories and files up to 4 KiB through the editor.'
if (Test-Path $qemu) {
  Write-Host 'Launching QEMU with only the virtual Aarambh disk attached...'
  & $qemu -drive "file=$image,format=raw,if=ide,index=0" -boot c
} else { Write-Host "QEMU not found at $qemu. Image built; edit `$qemu to launch it." }
