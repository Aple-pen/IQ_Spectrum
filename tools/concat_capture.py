#!/usr/bin/env python3
"""캡처 폴더(NNNNNN.bin 프레임 파일들)를 하나의 .bin 파일로 이어붙인다.

기본 동작은 순수 이어붙이기(plain concat)로, 프레임 payload를 순서대로
그대로 연결한다. Receive Mode 캡처가 파일 하나로 저장되기 전(구버전)에
만들어진 폴더들을 하나로 합칠 때 사용한다.

--hmft 를 주면 프레임마다 앞에 16바이트 HMFT 헤더를 삽입한다. 실제 장비는
2048 payload 당 16바이트 헤더를 넣어 스트림하지만 캡처는 payload만 저장하므로,
광대역 재생용 파일을 만들 때 헤더를 재구성하는 용도다.
(tools/concat_capture.sh / .ps1 의 기본 동작과 동일)

헤더(16 byte, big-endian):
  [0]  4B Magic  = "HMFT" (0x484D4654)
  [4]  4B Sequence Counter (프레임마다 +1, 기본 시작 0)
  [8]  4B high user space: bit[31:29]=대역폭 코드, bit[28:0]=Center 주파수(kHz)
  [12] 4B low user space = 0 (reserved)
대역폭 코드: 200M=0, 100M=1, 20M=2, 10M=3, 5M=4

사용법:
  # 폴더 하나를 "<폴더명>.bin" 으로 합치기
  python tools/concat_capture.py "build/Debug/20260915 132415 _ 2.4 GHz High"

  # 여러 폴더를 한 번에
  python tools/concat_capture.py "build/Debug/20260915 1324"*

  # 부모 폴더 아래 캡처 폴더를 전부
  python tools/concat_capture.py --all build/Debug

  # 출력 경로 지정
  python tools/concat_capture.py <folder> -o merged.bin

  # 광대역 재생용 (HMFT 헤더 삽입, 20MHz / 900.000MHz)
  python tools/concat_capture.py --hmft --bw-code 2 --center-khz 900000 <folder>
"""

import argparse
import os
import re
import struct
import sys

FRAME_RE = re.compile(r"^(\d+)\.bin$")
HMFT_MAGIC = b"HMFT"
HEADER_BYTES = 16


def frame_files(folder):
    """폴더 안의 프레임 파일을 인덱스 순으로 반환. (이름, 인덱스) 리스트."""
    found = []
    for name in os.listdir(folder):
        m = FRAME_RE.match(name)
        if m and os.path.isfile(os.path.join(folder, name)):
            found.append((name, int(m.group(1))))
    found.sort(key=lambda item: item[1])
    return found


def is_capture_folder(path):
    return os.path.isdir(path) and bool(frame_files(path))


def make_header(seq, high):
    return HMFT_MAGIC + struct.pack(">III", seq & 0xFFFFFFFF, high, 0)


def merge(folder, out_path, args):
    files = frame_files(folder)
    if not files:
        print("  skip: NNNNNN.bin 프레임 파일이 없음", file=sys.stderr)
        return False

    indices = [idx for _, idx in files]
    missing = sorted(set(range(indices[0], indices[-1] + 1)) - set(indices))
    print("  frames: %d (index %d-%d)" % (len(files), indices[0], indices[-1]))
    if missing:
        preview = ", ".join(str(i) for i in missing[:10])
        more = " ..." if len(missing) > 10 else ""
        print("  warning: 인덱스 누락 %d개: %s%s" % (len(missing), preview, more),
              file=sys.stderr)
        if not args.allow_gaps:
            print("  aborted: 누락을 무시하려면 --allow-gaps 를 붙일 것",
                  file=sys.stderr)
            return False

    if os.path.exists(out_path) and not args.force:
        print("  aborted: 출력 파일이 이미 있음 (--force 로 덮어쓰기): %s"
              % out_path, file=sys.stderr)
        return False

    high = ((args.bw_code & 0x7) << 29) | (args.center_khz & 0x1FFFFFFF)
    if args.hmft:
        print("  header: magic=HMFT high=0x%08X (BW code=%d, center=%d kHz)"
              % (high, args.bw_code, args.center_khz))

    sizes = {}
    payload_total = 0
    seq = args.start_seq
    with open(out_path, "wb") as out:
        for name, _ in files:
            with open(os.path.join(folder, name), "rb") as src:
                data = src.read()
            sizes[len(data)] = sizes.get(len(data), 0) + 1
            payload_total += len(data)
            if args.hmft:
                out.write(make_header(seq, high))
                seq += 1
            out.write(data)

    expected = payload_total + (len(files) * HEADER_BYTES if args.hmft else 0)
    actual = os.path.getsize(out_path)
    size_desc = ", ".join("%dB x%d" % (size, count)
                          for size, count in sorted(sizes.items()))
    print("  payload: %s" % size_desc)
    print("  output: %s (%d bytes, %.2f MB)"
          % (out_path, actual, actual / (1024.0 * 1024.0)))
    if actual != expected:
        print("  FAILED: 크기 불일치 (기대 %d, 실제 %d)" % (expected, actual),
              file=sys.stderr)
        return False
    print("  verify: OK")
    return True


def main():
    # 파이프로 넘길 때 stdout(진행 로그)과 stderr(경고)의 순서가 뒤섞이지 않도록.
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(
        description="캡처 폴더의 NNNNNN.bin 프레임들을 .bin 하나로 합친다.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("사용법:", 1)[1].strip() if "사용법:" in __doc__ else None)
    parser.add_argument("folders", nargs="+", metavar="FOLDER",
                        help="캡처 폴더 경로 (--all 이면 부모 폴더)")
    parser.add_argument("-o", "--output", metavar="PATH",
                        help="출력 파일 경로 (폴더 1개일 때만. 기본: <폴더명>.bin)")
    parser.add_argument("--all", action="store_true",
                        help="인자를 부모 폴더로 보고, 그 아래 캡처 폴더를 전부 처리")
    parser.add_argument("-f", "--force", action="store_true",
                        help="출력 파일이 이미 있어도 덮어쓴다")
    parser.add_argument("--allow-gaps", action="store_true",
                        help="프레임 인덱스가 누락돼도 계속 진행")
    parser.add_argument("--hmft", action="store_true",
                        help="프레임마다 16바이트 HMFT 헤더를 삽입 (광대역 재생용)")
    parser.add_argument("--bw-code", type=int, default=2, metavar="N",
                        help="HMFT 대역폭 코드 (200M=0,100M=1,20M=2,10M=3,5M=4). 기본 2")
    parser.add_argument("--center-khz", type=int, default=900000, metavar="KHZ",
                        help="HMFT Center 주파수(kHz). 기본 900000")
    parser.add_argument("--start-seq", type=int, default=0, metavar="N",
                        help="HMFT Sequence Counter 시작값. 기본 0")
    args = parser.parse_args()

    targets = []
    for path in args.folders:
        path = path.rstrip("/\\")
        if args.all:
            if not os.path.isdir(path):
                print("error: not a directory: %s" % path, file=sys.stderr)
                return 1
            children = [os.path.join(path, n) for n in sorted(os.listdir(path))]
            found = [c for c in children if is_capture_folder(c)]
            if not found:
                print("error: 캡처 폴더를 찾지 못함: %s" % path, file=sys.stderr)
                return 1
            targets.extend(found)
        else:
            if not os.path.isdir(path):
                print("error: not a directory: %s" % path, file=sys.stderr)
                return 1
            targets.append(path)

    if args.output and len(targets) != 1:
        print("error: --output 은 폴더가 1개일 때만 쓸 수 있음 (대상 %d개)"
              % len(targets), file=sys.stderr)
        return 1

    ok_count = 0
    for folder in targets:
        out_path = args.output or (folder.rstrip("/\\") + ".bin")
        print("[%s]" % folder)
        if merge(folder, out_path, args):
            ok_count += 1
        print()

    print("done: %d/%d" % (ok_count, len(targets)))
    return 0 if ok_count == len(targets) else 1


if __name__ == "__main__":
    sys.exit(main())
