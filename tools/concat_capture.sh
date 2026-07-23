#!/usr/bin/env bash
# 캡처 폴더(NNNNNN.bin 프레임 파일들)를 하나의 재생용 .bin으로 이어붙인다.
# 프레임(2048B payload)마다 앞에 16바이트 HMFT 헤더를 삽입한다.
#
# 실제 장비는 2048 payload 당 16바이트 헤더를 삽입해 스트림한다. 캡처는 payload만
# 파일로 저장하므로, 재생용 파일을 만들 때 헤더를 재구성한다.
#
# 헤더(16 byte, big-endian):
#   [0]  4B Magic  = "HMFT" (0x484D4654)
#   [4]  4B Sequence Counter (프레임마다 +1, 기본 시작 0)
#   [8]  4B high user space: bit[31:29]=대역폭 코드, bit[28:0]=Center 주파수(kHz)
#   [12] 4B low user space = 0 (reserved)
# 대역폭 코드: 200M=0, 100M=1, 20M=2, 10M=3, 5M=4
#
# 사용법:
#   tools/concat_capture.sh "<capture-folder>" [output.bin] [bwCode] [centerKHz] [startSeq]
# 예:
#   tools/concat_capture.sh "/d/Record 패킷/broadband_test/20260720 104129"
#
# Windows 환경에서는 tools/concat_capture.ps1 (PowerShell 버전)을 사용할 것.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <capture-folder> [output.bin] [bwCode] [centerKHz] [startSeq]" >&2
  exit 1
fi

src="$1"
out="${2:-${src%/}.bin}"
bwCode="${3:-2}"        # 기본 20MHz
centerKHz="${4:-900000}" # 기본 900.000MHz
startSeq="${5:-0}"

if [[ ! -d "$src" ]]; then
  echo "error: not a directory: $src" >&2
  exit 1
fi

# high user space = [bwCode(3bit)][centerKHz(29bit)]
high=$(( ((bwCode & 0x7) << 29) | (centerKHz & 0x1FFFFFFF) ))
printf 'header: magic=HMFT  high=0x%08X (BW code=%d, center=%d kHz)  low=0x00000000\n' \
  "$high" "$bwCode" "$centerKHz"

# 파일명이 zero-padding(000001.bin ...)이라 lexical sort == numeric sort.
mapfile -d '' -t files < <(find "$src" -maxdepth 1 -type f -name '*.bin' -print0 | sort -z)
echo "frames: ${#files[@]}"
echo "output: $out"

: > "$out"
seq=$startSeq
for f in "${files[@]}"; do
  # 16바이트 헤더 (big-endian) 방출
  printf "$(printf '\\x%02x' \
    0x48 0x4d 0x46 0x54 \
    $(( (seq >> 24) & 0xFF )) $(( (seq >> 16) & 0xFF )) $(( (seq >> 8) & 0xFF )) $(( seq & 0xFF )) \
    $(( (high >> 24) & 0xFF )) $(( (high >> 16) & 0xFF )) $(( (high >> 8) & 0xFF )) $(( high & 0xFF )) \
    0 0 0 0 )" >> "$out"
  cat "$f" >> "$out"
  seq=$(( seq + 1 ))
done

echo "done: $(stat -c%s "$out") bytes"
