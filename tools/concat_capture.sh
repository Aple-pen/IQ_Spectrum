#!/usr/bin/env bash
# 캡처 폴더(NNNNNN.bin 프레임 파일들)를 하나의 재생용 .bin으로 이어붙인다.
# 각 프레임 파일은 캡처 시 STX/ETX payload 1개 단위로 저장되어 있으므로,
# 파일명 오름차순으로 concat하면 원본 스트림과 동일한 바이트열이 된다.
#
# 사용법:
#   tools/concat_capture.sh "<capture-folder>" [output.bin]
# 예:
#   tools/concat_capture.sh "/d/Record 패킷/broadband_test/20260720 104129"
#
# 출력 파일을 지정하지 않으면 "<capture-folder>.bin"으로 만든다.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <capture-folder> [output.bin]" >&2
  exit 1
fi

src="$1"
out="${2:-${src%/}.bin}"

if [[ ! -d "$src" ]]; then
  echo "error: not a directory: $src" >&2
  exit 1
fi

# 파일명이 zero-padding(000001.bin ...)이라 lexical sort == numeric sort.
# 파일 수가 많을 수 있으므로 glob 대신 find | sort | xargs cat 사용.
count=$(find "$src" -maxdepth 1 -type f -name '*.bin' | wc -l)
echo "frames: $count"
echo "output: $out"

: > "$out"
find "$src" -maxdepth 1 -type f -name '*.bin' -print0 \
  | sort -z \
  | xargs -0 cat >> "$out"

echo "done: $(stat -c%s "$out") bytes"
