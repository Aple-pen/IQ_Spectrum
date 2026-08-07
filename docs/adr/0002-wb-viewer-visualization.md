status: Accepted · date: 2026-07-29 · deciders: S/W 연구소

# ADR-0002 광대역 WB Viewer 수신·표시 정책 (WBSG/ZeroMQ)

## Context

wb_scanner(Jetson)가 광대역 스캔 **1사이클 스냅샷**(WBSG v2)을 ZeroMQ PUB/SUB 로
발행한다(전송·포맷 규격은 ICD-0001). 한 메시지 = 한 사이클이고, 사이클은
대역(기본 800~6000 MHz, 200 MHz 슬라이스)을 한 바퀴 돈 것으로, 각 슬라이스는
그 center 에 머무는 동안(dwell) 수신한 스펙트럼 행들을 담는다.

기존 뷰어는 파일/TCP 기반의 IQ·HMFT 경로(Playback / Receive)만 있었다. 이 경로는
전송(raw TCP + STX/ETX)과 포맷(HMFT 헤더 + 2048 payload)이 WBSG 와 완전히 다르고,
1채널 시간영역 파형 또는 IQ FFT 를 그리는 구조라 WBSG 사이클을 그대로 태울 수 없다.

또한 표시 측에 성능 제약이 있다.

- `ImPlot::PlotHeatmap` 은 **셀 1개당 사각형 1개**를 draw list 에 넣는다. 즉 렌더
  비용은 셀 수(행×열)에 정비례한다.
- WBSG 밴드 전체 열 수 = `expectedSlices × validSamples`(cf=8 기준 26×209 ≈ 5434).
  사이클당 dwell 행은 기본 480, 가변.
- 다운샘플 없이 그리면 스펙트로그램이 수백만 셀/프레임이 되어 프레임률이 급락한다.

정리하면 (1) WBSG 를 받는 독립 경로, (2) 긴 밴드 스펙트럼을 읽기 쉽게 그리는 방법,
(3) dwell 시간구조를 보존하되 렌더 비용을 상한으로 묶는 방법이 필요하다.

## Options

수신 경로

A. 기존 Receive(IqReceiver) 모드를 확장해 WBSG 도 처리
   - 모드가 하나로 유지됨
   - 전송(ZeroMQ) · 포맷(WBSG) · 데이터 의미가 전혀 달라 분기 조건이 복잡해짐
   - IQ/HMFT 전용 컨트롤(FFT/샘플포맷/채널/오프셋)이 뒤섞임

B. **별도 Wideband(WB Viewer) 모드**를 신설 (독립 브랜치)
   - 전송/파서/렌더 소스를 깔끔히 분리
   - 기존 두 모드에 영향 없음
   - 모드 수가 하나 늘고, 공용 스냅샷 구조에 필드가 추가됨

의존성

A. vcpkg 등 외부 패키지로 libzmq 링크
B. **FetchContent 로 libzmq 를 소스에서 정적 빌드** → 단일 바이너리
   - glfw/imgui/implot 와 동일한 방식, 무의존 exe
   - 첫 configure 에서 libzmq 를 빌드(수 분)

스펙트로그램 표시

A. 사이클 dwell 를 1행으로 압축(max-hold)해 사이클당 1행만 누적
   - 가장 가벼움
   - 사이클 내부 시간구조가 사라짐(버스트 duty 관측 불가)
B. dwell 전 행을 그대로 누적
   - 시간구조 보존
   - 480행 × 8사이클/s = 3840행/s 라 스크롤이 눈으로 따라갈 수 없고, 렌더 폭발
C. **사이클을 고정 행수로 decimation 하고, 표시 총량과 렌더 셀 수에 상한**
   - 시간구조를 유지하면서 스크롤 속도·렌더 비용을 제어
   - decimation/상한 값을 정해야 함

## Decision

수신 경로는 **B(별도 Wideband 모드)**, 의존성은 **B(FetchContent 정적 libzmq)**,
스펙트로그램은 **C(고정 decimation + 표시/렌더 상한)** 를 채택한다.

### 1. 수신 (WbViewerReceiver)

- ZeroMQ `SUB` 로 `tcp://<scanner-ip>:<port>`(기본 5557)에 connect, topic(기본
  `"WB"`) 구독, `recv_multipart` 의 **마지막 프레임**을 payload 로 취한다.
- `ZMQ_RCVHWM=4`, `ZMQ_RCVTIMEO=200ms`(중단 응답성), `ZMQ_LINGER=0`.
- WBSG v2 파싱은 ICD-0001 준수: magic/version 검사, `headerBytes`/`sliceEntryBytes`
  로 건너뛰기(크기 하드코딩 금지), 블록은 `dataOffset` 절대 오프셋 접근, 값 복원은
  `raw − dcOffset`, 전부 little-endian. `cycleIndex` 불연속(드롭)·`complete==0`
  (슬라이스 결손)은 정상 케이스로 처리한다.
- libzmq 는 FetchContent(v4.3.5)로 정적 빌드(`BUILD_SHARED=OFF`, CURVE/draft/docs
  /tests off), `ZMQ_STATIC` 정의 후 링크 → 단일 바이너리.

### 2. 주파수축 조립 (밴드 이어붙이기)

- 슬라이스는 `gridIndex × validSamples` 위치의 열 구간에 놓아 밴드 전체를 잇는다.
- 열 j 의 중심 주파수 = `bandStartKHz + (j + 0.5) × planBwKHz / validSamples` [kHz].
- 미수신(grid 결손) 열은 최솟값으로 메워 라인/히트맵이 튀지 않게 한다.
- 열 해상도는 producer 의 `colFactor`(validSamples)를 그대로 따른다(뷰어는 추가
  주파수 decimation 을 하지 않는다).

### 3. 상단 스펙트럼 라인 — 사이클 dwell 순차 재생

- 한 사이클의 dwell 행을 **매 렌더 프레임 1행씩 순차적으로** 그린다(라이브 스윕).
- 다 그리기 전에 다음 사이클이 오면(`fftFrameCount` 변화) **그 사이클의 0행부터
  다시** 시작한다. 마지막 행에서는 다음 사이클까지 홀드.
- y 오토스케일은 **사이클 전체(모든 행) 기준으로 사이클당 1회 고정** → 행마다 축이
  튀지 않는다.

### 4. 스펙트로그램 — decimation + 상한 (핵심 정책)

- **각 사이클을 80행으로 decimation** 한다. dwell 원본 행을 80등분해 구간별
  **max-pool**(평균 아님 — 좁은 버스트 보존)로 줄인다. (`Lines/cycle` 기본 80,
  0=auto=실제 dwell 행수)
- 스펙트로그램에 표시하는 **행은 무조건 최대 240행**. History rows 를 더 크게 잡아
  오래 저장해도 화면에는 240행까지만 그린다. `240 = 최근 3사이클(3×80)`.
- 저장은 풀 해상도 링버퍼(History rows × 밴드 열), 표시 직전에만 다운샘플한다.
- 렌더 셀 수 상한: 행 ≤ 240, 열은 **실제 플롯 픽셀 폭**을 넘지 않게 max-pool
  (표시 열 ≤ 1024). 결과적으로 셀 수 `≲ 240 × 1024 ≈ 25만`으로 프레임마다 고정된다.
- 단(tier) 분할 시 각 스펙트럼 단 바로 아래에 같은 주파수 구간의 스펙트로그램을
  배치하고, `BeginAlignedPlots` 로 x축을 정렬한다.

### 값·상수 요약

```text
transport   ZeroMQ SUB, topic "WB", tcp://<ip>:5557
payload     WBSG v2 (ICD-0001), little-endian, raw - dcOffset
band cols   expectedSlices × validSamples   (열 위치 = gridIndex × validSamples)
freq(j)     bandStartKHz + (j+0.5) × planBwKHz / validSamples  [kHz]
spectrum    사이클 dwell 를 프레임당 1행씩 순차 재생, 새 사이클이면 0행부터
sgram/cycle 80행 (max-pool decimation)
sgram max   240행 (무조건 상한 = 최근 3사이클)
render cap  행 ≤ 240, 열 ≤ min(플롯 픽셀폭, 1024)  → ~25만 셀/프레임
```

## Consequences

+ WBSG 사이클을 기존 tier 스펙트럼 + 단별 스펙트로그램 렌더에 그대로 태울 수 있다
  (수신기가 `frequencies`/`magnitudesDb`/dwell 블록만 채우면 됨).
+ 단일 바이너리(정적 libzmq)로 배포된다.
+ 사이클 내부 시간구조(버스트 duty)를 80행 해상도로 보존하면서, 최근 3사이클을
  한눈에 본다.
+ `Show spectrogram`·History rows·Lines/cycle 값과 무관하게 렌더 비용이 상한으로
  묶여 프레임률이 안정적이다.
+ 스펙트럼 라인은 "지금 이 순간의 한 행", 스펙트로그램은 "누적 시간 이력"으로 역할이
  분리된다.

- 스펙트럼 라인은 프레임당 1행 전진이라, 60fps·8사이클/s 에서는 각 사이클의 앞부분
  행들만 빠르게 갱신되어 보인다(dwell 전체 시간구조는 스펙트로그램에서 확인).
- 표시 최대 240행 상한은 하드코딩(`kMaxSpectrogramRows`)이라, 더 긴 시간창을 보려면
  코드 변경이 필요하다.
- 열 해상도는 producer `colFactor` 에 종속된다. 원본 1666 해상도를 보려면 producer
  측 `--viewer-cols` 를 낮춰야 하고(대역폭 급증, ICD-0001 §7), 뷰어는 무변경으로
  따라간다.
- 첫 빌드에서 libzmq 소스 컴파일 시간이 추가된다.
- IQ/HMFT 전용 컨트롤(FFT/샘플포맷/채널/HMFT/Constellation/오프셋)은 WB 모드에서
  숨겨진다(WBSG 는 자기기술 포맷).
