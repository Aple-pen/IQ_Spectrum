<#
.SYNOPSIS
  캡처 폴더(NNNNNN.bin 프레임 파일들)를 하나의 재생용 .bin으로 이어붙인다.
  프레임(2048B payload)마다 앞에 16바이트 HMFT 헤더를 삽입한다.

.DESCRIPTION
  실제 장비는 2048 payload 당 16바이트 헤더를 삽입하는 형태로 스트림한다.
  캡처는 payload만 파일로 저장하므로, 재생용 파일을 만들 때 헤더를 재구성한다.

  헤더(16 byte, big-endian):
    [0]  4B Magic  = "HMFT" (0x484D4654)
    [4]  4B Sequence Counter (프레임마다 +1, 기본 시작 0)
    [8]  4B high user space:
           bit[31:29] = Scan 대역폭 코드, bit[28:0] = Center 주파수(kHz)
    [12] 4B low user space = 0 (reserved)

  대역폭 코드: 200M=0, 100M=1, 20M=2, 10M=3, 5M=4

.PARAMETER Source
  캡처 폴더 경로. 예: "D:\Record 패킷\broadband_test\20260720 104129"

.PARAMETER Output
  출력 .bin 경로. 생략 시 "<Source>.bin".

.PARAMETER BwCode
  대역폭 코드(0..4). 기본 2 (20MHz).

.PARAMETER CenterKHz
  Center 주파수(kHz, 0..0x1FFFFFFF). 기본 900000 (900.000MHz).

.PARAMETER StartSeq
  Sequence Counter 시작값. 기본 0.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\concat_capture.ps1 "D:\Record 패킷\broadband_test\20260720 104129"
#>
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Source,
  [Parameter(Position = 1)]
  [string]$Output,
  [int]$BwCode = 2,
  [long]$CenterKHz = 900000,
  [long]$StartSeq = 0
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
  Write-Error "not a directory: $Source"
  exit 1
}
if ($BwCode -lt 0 -or $BwCode -gt 7) {
  Write-Error "BwCode must be 0..7 (top 3 bits)"; exit 1
}
if ($CenterKHz -lt 0 -or $CenterKHz -gt 0x1FFFFFFF) {
  Write-Error "CenterKHz must fit in 29 bits (0..$([int]0x1FFFFFFF))"; exit 1
}

if ([string]::IsNullOrEmpty($Output)) {
  $Output = ($Source.TrimEnd('\', '/')) + '.bin'
}

# high user space = [BwCode(3bit)][CenterKHz(29bit)]
$high = ([uint32]$BwCode -shl 29) -bor ([uint32]$CenterKHz -band 0x1FFFFFFF)
$low = [uint32]0
Write-Host ("header: magic=HMFT  high=0x{0:X8} (BW code={1}, center={2} kHz)  low=0x{3:X8}" -f $high, $BwCode, $CenterKHz, $low)

# 16바이트 헤더 (big-endian). seq는 프레임마다 갱신.
$hdr = New-Object byte[] 16
$hdr[0] = 0x48; $hdr[1] = 0x4D; $hdr[2] = 0x46; $hdr[3] = 0x54  # "HMFT"
$hdr[8] = [byte](($high -shr 24) -band 0xFF)
$hdr[9] = [byte](($high -shr 16) -band 0xFF)
$hdr[10] = [byte](($high -shr 8) -band 0xFF)
$hdr[11] = [byte]($high -band 0xFF)
$hdr[12] = [byte](($low -shr 24) -band 0xFF)
$hdr[13] = [byte](($low -shr 16) -band 0xFF)
$hdr[14] = [byte](($low -shr 8) -band 0xFF)
$hdr[15] = [byte]($low -band 0xFF)

# 파일명이 zero-padding(000001.bin ...)이라 이름 정렬 == 숫자 정렬.
$files = Get-ChildItem -LiteralPath $Source -Filter '*.bin' -File | Sort-Object Name
Write-Host "frames: $($files.Count)"
Write-Host "output: $Output"

$out = [System.IO.File]::Create($Output)
try {
  $buffer = New-Object byte[] 65536
  $seq = [uint32]$StartSeq
  foreach ($f in $files) {
    # Sequence Counter (big-endian) 갱신
    $hdr[4] = [byte](($seq -shr 24) -band 0xFF)
    $hdr[5] = [byte](($seq -shr 16) -band 0xFF)
    $hdr[6] = [byte](($seq -shr 8) -band 0xFF)
    $hdr[7] = [byte]($seq -band 0xFF)
    $out.Write($hdr, 0, 16)

    $in = [System.IO.File]::OpenRead($f.FullName)
    try {
      while (($read = $in.Read($buffer, 0, $buffer.Length)) -gt 0) {
        $out.Write($buffer, 0, $read)
      }
    }
    finally { $in.Dispose() }

    $seq = [uint32]($seq + 1)
  }
}
finally { $out.Dispose() }

$size = (Get-Item -LiteralPath $Output).Length
Write-Host "done: $size bytes"
