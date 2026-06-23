status: Accepted · date: 2026-06-23 · deciders: S/W 연구소

# ADR-0001 수신 FFT 기준 chunk 자동 계산 및 CHNK 협상 도입

## Context

receive 모드에서는 FFT 1회를 수행하려면 최소한 `fftSize` 만큼의 샘플이 필요하다. 그런데 기존 구현은 다음 두 문제가 있었다.

- 수신 측 `chunkBytes`가 FFT 설정과 별도로 관리되어, 실제 FFT 1회에 필요한 바이트 수보다 작게 설정될 수 있었다.
- 클라이언트가 서버에 자신이 원하는 스트림 payload 크기를 전달할 방법이 없어서, 서버는 항상 자신의 기본 chunk 크기로만 송신했다.

이 상태에서는 클라이언트가 작은 payload를 여러 번 받아 내부에서 다시 누적해야 했고, 서버 측 송신 단위와 클라이언트 FFT 처리 단위가 쉽게 어긋났다.

## Options

A. 수신 측에서만 로컬 누적 처리

- 서버 수정 없이 바로 적용 가능
- 구현 범위가 가장 작음
- 서버 송신 chunk 크기는 계속 비최적 상태로 남음

B. receive 모드에서 FFT 기준 최소 바이트를 자동 계산하고, 클라이언트가 서버에 요청

- 클라이언트 FFT 조건과 서버 송신 단위를 정렬할 수 있음
- 기존 프레임 포맷은 유지 가능
- 연결 직후 경량 handshake를 추가해야 함

C. 서버가 고정된 단일 정책으로 chunk 크기를 강제

- 서버 구현은 단순할 수 있음
- 클라이언트별 FFT 설정 차이를 반영할 수 없음
- 다양한 sample format / channel 조합에 유연하지 않음

## Decision

B를 채택한다.

receive 모드의 `chunkBytes`는 사용자가 수동으로 맞추는 값이 아니라, 현재 FFT 설정으로부터 자동 계산한다.

계산식은 다음과 같다.

```text
minChunkBytesForFft = fftSize * channels * bytesPerSample
```

`bytesPerSample`은 sample format에 따라 결정한다.

- `UInt8` = 1 byte
- `Int16LE` = 2 bytes
- `Float32LE` = 4 bytes

또한 TCP 연결 직후 클라이언트가 서버에 8바이트 `CHNK` handshake를 보내 원하는 chunk 크기를 전달한다.

```text
Offset  Size  Meaning
0       4     Magic = 'CHNK' (0x43 0x48 0x4E 0x4B, big-endian)
4       4     requestedChunkBytes (uint32, big-endian)
```

서버는 이 handshake를 optional로 처리한다.

- 짧은 timeout 안에 정상 handshake를 받으면 해당 값을 active chunk 크기로 사용한다.
- handshake가 없으면 기존 기본 `chunkBytes`로 계속 송신한다.
- handshake가 손상되었거나 값이 비정상이면 해당 연결은 protocol error로 본다.

payload 본문 프레임 형식은 바꾸지 않는다.

```text
STX(4B, BE) + LEN(4B, BE) + payload(LEN) + ETX(4B, BE)
```

## Consequences

+ receive 모드에서 FFT 1회 수행에 필요한 최소 바이트 수가 설정값과 자동으로 일치한다.
+ 서버와 클라이언트가 모두 최신 구현이면, 서버 송신 chunk 크기가 클라이언트 FFT 요구 크기에 맞춰진다.
+ receive UI에서 자동 계산된 바이트 수와 FFT window 시간을 바로 확인할 수 있다.
+ 구버전 클라이언트는 handshake 없이도 새 서버에 접속 가능하다.
+ 새 클라이언트는 구버전 서버에 붙더라도 로컬 누적 처리로 FFT 동작을 유지할 수 있다.

- 연결 직후 `CHNK` handshake라는 작은 프로토콜 확장이 추가된다.
- 서버가 handshake를 전혀 읽지 않는 구버전 구현이라면, 새 클라이언트와의 완전한 협상은 되지 않는다.
- malformed handshake는 해당 클라이언트 연결 실패 원인이 될 수 있다.
- negotiated chunk 크기는 연결 단위로만 적용되며 별도 영구 설정으로 저장되지는 않는다.
