#include "WbViewerReceiver.h"

#include <zmq.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
// WBSG 는 전부 little-endian. 호스트(x86/x64)도 LE 이므로 memcpy 로 그대로 읽는다.
template <class T> T RdLE(const uint8_t *p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}

// WRSP/WCMD 등 제어 메시지는 big-endian. 응답 파싱용.
uint32_t ReadBE32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// WbBinHeader 필드 오프셋 (ICD-0001 §4).
constexpr size_t H_MAGIC = 0;
constexpr size_t H_VERSION = 4;
constexpr size_t H_HEADER_BYTES = 8;
constexpr size_t H_SLICE_ENTRY_BYTES = 12;
constexpr size_t H_CYCLE_INDEX = 16;
constexpr size_t H_BAND_START_KHZ = 20;
constexpr size_t H_BAND_END_KHZ = 24;
constexpr size_t H_PLAN_BW_KHZ = 28;
constexpr size_t H_VALID_SAMPLES = 32;
constexpr size_t H_SLICE_COUNT = 36;
constexpr size_t H_EXPECTED_SLICES = 40;
constexpr size_t H_DC_OFFSET = 44;
constexpr size_t H_COMPLETE = 52;
constexpr size_t H_MIN_BYTES = 64; // v1 헤더 최소 크기
// 지원하는 최대 WBSG 버전. v3 = 슬라이스별 bwKHz, v4 = 슬라이스별 validSamples +
// zone 표. 헤더/엔트리 앞부분 오프셋은 버전 간 동일하고 필드는 뒤에만 붙으므로,
// headerBytes/sliceEntryBytes 로 건너뛰면서 파싱한다(ICD §3.3).
constexpr uint32_t WB_MAX_VERSION = 4;

// WbBinSliceEntry 필드 오프셋 (ICD-0001 §5 + v3/v4 확장).
constexpr size_t E_CENTER_KHZ = 0;
constexpr size_t E_GRID_INDEX = 8;
constexpr size_t E_ROWS = 16;
constexpr size_t E_BW_KHZ = 20;       // v3+ (v2 까지는 reserved=0)
constexpr size_t E_DATA_OFFSET = 32;
constexpr size_t E_VALID_SAMPLES = 40; // v4+

// 밴드 전체 열 수 상한(방어). 원본 열 합이 이보다 크면 폐기.
constexpr size_t kMaxBandCols = 4u * 1024u * 1024u;
// 균일 주파수 격자의 최대 열 수. 가장 촘촘한 zone 해상도를 따라가되 메모리/CPU 를
// 묶는다(스펙트로그램 링버퍼 = History rows × 이 값).
constexpr size_t kMaxUniformCols = 32768;
} // namespace

bool WbViewerReceiver::RequestZoneConfig(const std::string &ip, uint16_t port,
                                         const std::vector<uint8_t> &request,
                                         std::string &error,
                                         uint32_t &respStatus,
                                         std::string &respMagic) {
  respStatus = 0;
  respMagic.clear();
  void *ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    error = "zmq_ctx_new failed";
    return false;
  }
  void *req = zmq_socket(ctx, ZMQ_REQ);
  if (req == nullptr) {
    zmq_ctx_term(ctx);
    error = "zmq_socket(REQ) failed";
    return false;
  }
  const int timeoutMs = 1000; // 상위설계 7.7항 제안값
  const int linger = 0;
  zmq_setsockopt(req, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));
  zmq_setsockopt(req, ZMQ_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
  zmq_setsockopt(req, ZMQ_LINGER, &linger, sizeof(linger));

  const std::string endpoint = "tcp://" + ip + ":" + std::to_string(port);
  bool ok = false;
  do {
    if (zmq_connect(req, endpoint.c_str()) != 0) {
      error = "connect " + endpoint + " failed: " + zmq_strerror(zmq_errno());
      break;
    }
    if (zmq_send(req, request.data(), request.size(), 0) < 0) {
      error = std::string("send failed: ") + zmq_strerror(zmq_errno());
      break;
    }
    uint8_t resp[64];
    const int n = zmq_recv(req, resp, sizeof(resp), 0);
    if (n < 0) {
      error = (zmq_errno() == EAGAIN)
                  ? "response timeout (no WRSP from " + endpoint + ")"
                  : std::string("recv failed: ") + zmq_strerror(zmq_errno());
      break;
    }
    if (n < 16) {
      error = "short WRSP response (" + std::to_string(n) + " bytes)";
      break;
    }
    // WRSP 16B: magic[4] + status(u32 BE) + num_zones(u32 BE) + reserved(u32 BE)
    respMagic.assign(reinterpret_cast<const char *>(resp), 4);
    respStatus = ReadBE32(resp + 4);
    ok = true;
  } while (false);

  zmq_close(req);
  zmq_ctx_term(ctx);
  return ok;
}

bool WbViewerReceiver::Start(const StreamConfig &config, std::string &error) {
  if (config.ip.empty()) {
    error = "Enter the scanner IP to connect to";
    return false;
  }
  if (config.port == 0) {
    error = "Port must be 1..65535";
    return false;
  }
  StartWorker(config);
  return true;
}

void WbViewerReceiver::Run(StreamConfig config) {
  lastCycleIndex_ = 0;
  haveLastCycle_ = false;
  dropCount_ = 0;
  cycleCount_ = 0;
  discardCount_ = 0;
  wbLinesPerCycle_ = config.wbLinesPerCycle;
  // 격자 latch 리셋 (새 스트림 = 플랜이 바뀌었을 수 있다)
  gridBandLoKHz_ = 0.0;
  gridBandHiKHz_ = 0.0;
  gridMinStepKHz_ = 0.0;
  gridStepKHz_ = 0.0;
  gridCols_ = 0;

  void *ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    SetStatus("ZMQ init failed", "zmq_ctx_new");
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }
  void *sub = zmq_socket(ctx, ZMQ_SUB);
  if (sub == nullptr) {
    zmq_ctx_term(ctx);
    SetStatus("ZMQ init failed", "zmq_socket");
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }

  // 얕은 큐 + 논블로킹 수신(타임아웃)으로 최신성 우선 + 깔끔한 중단 (ICD §1.3).
  const int rcvhwm = 4;
  const int rcvtimeo = 200; // ms — stopRequested_ 재확인 주기
  const int linger = 0;
  zmq_setsockopt(sub, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
  zmq_setsockopt(sub, ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
  zmq_setsockopt(sub, ZMQ_LINGER, &linger, sizeof(linger));
  zmq_setsockopt(sub, ZMQ_SUBSCRIBE, config.wbTopic.data(),
                 config.wbTopic.size());

  const std::string endpoint =
      "tcp://" + config.ip + ":" + std::to_string(config.port);
  if (zmq_connect(sub, endpoint.c_str()) != 0) {
    SetStatus("ZMQ connect failed", zmq_strerror(zmq_errno()));
    zmq_close(sub);
    zmq_ctx_term(ctx);
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.listening = false;
    snapshot_.connected = false;
    snapshot_.status =
        "SUB connect " + endpoint + " topic=\"" + config.wbTopic + "\" (waiting)";
  }

  std::vector<uint8_t> payload; // 마지막 프레임(=WBSG payload) 누적용
  while (!stopRequested_) {
    // 멀티파트 한 메시지를 통째로 받고, 마지막 프레임을 payload 로 취한다(ICD §1.2).
    bool gotMessage = false;
    bool recvError = false;
    while (true) {
      zmq_msg_t msg;
      zmq_msg_init(&msg);
      const int rc = zmq_msg_recv(&msg, sub, 0);
      if (rc < 0) {
        zmq_msg_close(&msg);
        if (zmq_errno() == EAGAIN) {
          // 타임아웃: 메시지 중간이 아니면 정상(대기 중). stop 재확인 후 재시도.
          break;
        }
        if (zmq_errno() == ETERM || zmq_errno() == EINTR) {
          recvError = true;
        }
        break;
      }
      // 마지막 프레임이 이기도록 매번 덮어쓴다.
      const auto *bytes = static_cast<const uint8_t *>(zmq_msg_data(&msg));
      const size_t n = zmq_msg_size(&msg);
      payload.assign(bytes, bytes + n);
      gotMessage = true;

      int more = 0;
      size_t moreSize = sizeof(more);
      zmq_getsockopt(sub, ZMQ_RCVMORE, &more, &moreSize);
      zmq_msg_close(&msg);
      if (more == 0) {
        break; // 멀티파트 끝
      }
    }

    if (recvError) {
      break;
    }
    if (!gotMessage) {
      continue; // 타임아웃 — stop 재확인 후 재수신
    }

    HandleCycle(payload.data(), payload.size());
  }

  zmq_close(sub);
  zmq_ctx_term(ctx);

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.streamRunning = false;
  snapshot_.connected = false;
  snapshot_.listening = false;
  if (snapshot_.error.empty()) {
    snapshot_.status = "Stopped";
  }
}

void WbViewerReceiver::NoteDiscard(const std::string &reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++discardCount_;
  snapshot_.connected = true; // 뭔가 오긴 왔다
  // 조용히 버리면 "데이터가 안 온다"와 구분이 안 되므로 사유를 노출한다.
  snapshot_.status = "discarded: " + reason + "  (" +
                     std::to_string(discardCount_) + " dropped, " +
                     std::to_string(cycleCount_) + " ok)";
}

void WbViewerReceiver::HandleCycle(const uint8_t *data, size_t len) {
  // --- 식별/버전 검사 (ICD §3.1) ---
  if (len < H_MIN_BYTES) {
    NoteDiscard("payload too small (" + std::to_string(len) + "B)");
    return;
  }
  if (std::memcmp(data + H_MAGIC, "WBSG", 4) != 0) {
    NoteDiscard("bad magic (not WBSG)");
    return;
  }
  const uint32_t version = RdLE<uint32_t>(data + H_VERSION);
  if (version < 1 || version > WB_MAX_VERSION) {
    NoteDiscard("unsupported WBSG version " + std::to_string(version));
    return;
  }

  const uint32_t headerBytes = RdLE<uint32_t>(data + H_HEADER_BYTES);
  const uint32_t sliceEntryBytes = RdLE<uint32_t>(data + H_SLICE_ENTRY_BYTES);
  const uint32_t cycleIndex = RdLE<uint32_t>(data + H_CYCLE_INDEX);
  const uint32_t planBwKHz = RdLE<uint32_t>(data + H_PLAN_BW_KHZ);
  const uint32_t hdrValidSamples = RdLE<uint32_t>(data + H_VALID_SAMPLES);
  const uint32_t sliceCount = RdLE<uint32_t>(data + H_SLICE_COUNT);
  const uint32_t expectedSlices = RdLE<uint32_t>(data + H_EXPECTED_SLICES);
  const int32_t dcOffset = RdLE<int32_t>(data + H_DC_OFFSET);
  const uint32_t complete = RdLE<uint32_t>(data + H_COMPLETE);

  // --- 정합성 검사 (ICD §3.3: 크기 하드코딩 금지, 오프셋으로 접근) ---
  // v4 부터 헤더의 planBwKHz / validSamples 는 zone 폭이 섞이면 0 이다. 실제 값은
  // 슬라이스별 bwKHz(offset 20) / validSamples(offset 40) 에 있으므로 여기서
  // 0 을 이유로 버리면 안 된다.
  if (headerBytes < H_MIN_BYTES || sliceEntryBytes < E_DATA_OFFSET + 8) {
    NoteDiscard("bad headerBytes/sliceEntryBytes");
    return;
  }
  const size_t entriesEnd = static_cast<size_t>(headerBytes) +
                            static_cast<size_t>(sliceCount) *
                                static_cast<size_t>(sliceEntryBytes);
  if (entriesEnd > len) {
    NoteDiscard("slice table exceeds payload");
    return;
  }

  // --- 유효 슬라이스 수집 (bounds 검사) ---
  // v3+ : 슬라이스마다 bwKHz, v4+ : 슬라이스마다 validSamples 를 갖는다.
  // 구버전은 헤더의 planBwKHz / validSamples 로 폴백한다.
  const bool haveSliceBw = sliceEntryBytes >= E_BW_KHZ + 4;
  const bool haveSliceValid = sliceEntryBytes >= E_VALID_SAMPLES + 4;
  struct SliceRef {
    uint32_t centerKHz;
    uint32_t bwKHz;
    uint32_t validSamples;
    uint32_t rows;
    uint64_t dataOffset;
  };
  std::vector<SliceRef> refs;
  refs.reserve(sliceCount);
  uint32_t maxRows = 0;
  size_t bandCols = 0;
  for (uint32_t i = 0; i < sliceCount; ++i) {
    const uint8_t *e =
        data + headerBytes + static_cast<size_t>(i) * sliceEntryBytes;
    SliceRef s{};
    s.centerKHz = RdLE<uint32_t>(e + E_CENTER_KHZ);
    s.rows = RdLE<uint32_t>(e + E_ROWS);
    s.dataOffset = RdLE<uint64_t>(e + E_DATA_OFFSET);
    s.bwKHz = haveSliceBw ? RdLE<uint32_t>(e + E_BW_KHZ) : 0;
    if (s.bwKHz == 0) {
      s.bwKHz = planBwKHz; // v1/v2 폴백
    }
    s.validSamples = haveSliceValid ? RdLE<uint32_t>(e + E_VALID_SAMPLES) : 0;
    if (s.validSamples == 0) {
      s.validSamples = hdrValidSamples; // v1~v3 폴백
    }
    if (s.rows == 0 || s.validSamples == 0 || s.bwKHz == 0) {
      continue;
    }
    // data 행렬이 payload 범위 안에 있는지 (ICD §8: 절대 오프셋으로 접근).
    const size_t blockBytes =
        static_cast<size_t>(s.rows) * static_cast<size_t>(s.validSamples);
    if (s.dataOffset > len || s.dataOffset + blockBytes > len) {
      continue;
    }
    refs.push_back(s);
    maxRows = std::max(maxRows, s.rows);
    bandCols += s.validSamples;
  }
  if (refs.empty()) {
    NoteDiscard("no usable slices (of " + std::to_string(sliceCount) + ")");
    return;
  }
  if (bandCols > kMaxBandCols) {
    NoteDiscard("band too wide (" + std::to_string(bandCols) + " cols)");
    return;
  }

  // --- 균일 주파수 격자 (플랜 기준으로 고정) ---
  // zone 마다 bw/validSamples 가 달라 "열당 주파수"가 최대 16배까지 차이 난다
  // (예: 12MHz/201열 = 59.7kHz vs 200MHz/209열 = 957kHz). 열을 그냥 이어붙이면
  // PlotHeatmap 이 열을 x축에 균등 분배하므로 스펙트럼 라인과 위치가 어긋난다.
  // 그래서 밴드 전체를 **균일한 kHz 격자**로 리샘플한다.
  //
  // 격자 범위는 이번 사이클의 슬라이스가 아니라 헤더의 플랜 범위(bandStart/End)
  // 로 잡는다. 부분 사이클(결손)에서 가장자리 슬라이스가 빠져도 축이 흔들리지
  // 않게 하기 위함. 간격도 지금까지 본 가장 촘촘한 값으로 latch 한다.
  double planLoKHz = static_cast<double>(RdLE<uint32_t>(data + H_BAND_START_KHZ));
  double planHiKHz = static_cast<double>(RdLE<uint32_t>(data + H_BAND_END_KHZ));
  double sliceMinStepKHz = std::numeric_limits<double>::max();
  {
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const SliceRef &s : refs) {
      const double sl =
          static_cast<double>(s.centerKHz) - static_cast<double>(s.bwKHz) * 0.5;
      lo = std::min(lo, sl);
      hi = std::max(hi, sl + static_cast<double>(s.bwKHz));
      sliceMinStepKHz =
          std::min(sliceMinStepKHz,
                   static_cast<double>(s.bwKHz) / static_cast<double>(s.validSamples));
    }
    // 헤더 플랜 범위가 없거나(구버전 0) 슬라이스가 그 밖으로 나가면 보정.
    if (!(planHiKHz > planLoKHz)) {
      planLoKHz = lo;
      planHiKHz = hi;
    } else {
      planLoKHz = std::min(planLoKHz, lo);
      planHiKHz = std::max(planHiKHz, hi);
    }
  }
  if (!(planHiKHz > planLoKHz) || !(sliceMinStepKHz > 0.0)) {
    NoteDiscard("degenerate band span");
    return;
  }

  // 플랜이 바뀌었을 때만 격자를 다시 잡는다(그 외에는 열 수·간격 고정).
  const bool planChanged = gridCols_ == 0 || planLoKHz != gridBandLoKHz_ ||
                           planHiKHz != gridBandHiKHz_;
  const bool finerSeen = !planChanged && sliceMinStepKHz < gridMinStepKHz_ * 0.999;
  if (planChanged || finerSeen) {
    gridBandLoKHz_ = planLoKHz;
    gridBandHiKHz_ = planHiKHz;
    gridMinStepKHz_ = planChanged
                          ? sliceMinStepKHz
                          : std::min(gridMinStepKHz_, sliceMinStepKHz);
    size_t c = static_cast<size_t>(
        std::ceil((gridBandHiKHz_ - gridBandLoKHz_) / gridMinStepKHz_));
    gridCols_ = std::clamp<size_t>(c, 1, kMaxUniformCols);
    gridStepKHz_ =
        (gridBandHiKHz_ - gridBandLoKHz_) / static_cast<double>(gridCols_);
  }

  const double bandLoKHz = gridBandLoKHz_;
  const size_t cols = gridCols_;
  const double gridStepKHz = gridStepKHz_;

  // --- 밴드 스펙트로그램 블록 조립 (n행 × cols) ---
  // 사이클 하나의 dwell 시간구조를 보존한다: n=auto 면 실제 최대 dwell 행 수.
  // n<rows 면 구간 max-hold(버스트 보존), n>=rows 면 최근접 행 복제.
  int n = wbLinesPerCycle_ > 0 ? wbLinesPerCycle_ : static_cast<int>(maxRows);
  n = std::clamp(n, 1, 4096);

  constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
  auto block = std::make_shared<std::vector<float>>(
      static_cast<size_t>(n) * cols, kMissing);
  float *blk = block->data();
  float minPresent = std::numeric_limits<float>::max();

  for (const SliceRef &s : refs) {
    const uint8_t *src = data + s.dataOffset;
    const int rows = static_cast<int>(s.rows);
    const uint32_t vs = s.validSamples;
    const double sliceLoKHz =
        static_cast<double>(s.centerKHz) - static_cast<double>(s.bwKHz) * 0.5;
    const double sliceStepKHz =
        static_cast<double>(s.bwKHz) / static_cast<double>(vs);

    // 이 슬라이스가 덮는 격자 bin 구간.
    long long b0 = static_cast<long long>(
        std::floor((sliceLoKHz - bandLoKHz) / gridStepKHz));
    long long b1 = static_cast<long long>(std::ceil(
        (sliceLoKHz + static_cast<double>(s.bwKHz) - bandLoKHz) / gridStepKHz));
    b0 = std::max<long long>(b0, 0);
    b1 = std::min<long long>(b1, static_cast<long long>(cols));

    for (long long b = b0; b < b1; ++b) {
      // 이 bin 이 덮는 원본 열 구간 [cLo, cHi). 격자가 더 촘촘하면 최근접 1열.
      const double binLoKHz =
          bandLoKHz + static_cast<double>(b) * gridStepKHz - sliceLoKHz;
      long long cLo = static_cast<long long>(std::floor(binLoKHz / sliceStepKHz));
      long long cHi = static_cast<long long>(
          std::ceil((binLoKHz + gridStepKHz) / sliceStepKHz));
      cLo = std::clamp<long long>(cLo, 0, static_cast<long long>(vs) - 1);
      cHi = std::clamp<long long>(cHi, cLo + 1, static_cast<long long>(vs));

      for (int k = 0; k < n; ++k) {
        const int rLo = static_cast<int>(static_cast<int64_t>(k) * rows / n);
        int rHi = (n >= rows)
                      ? std::min(rLo, rows - 1) + 1
                      : static_cast<int>(static_cast<int64_t>(k + 1) * rows / n);
        const int rStart = (n >= rows) ? std::min(rLo, rows - 1) : rLo;
        if (rHi <= rStart) {
          rHi = rStart + 1;
        }
        if (rHi > rows) {
          rHi = rows;
        }
        int best = -128;
        for (int r = rStart; r < rHi; ++r) {
          const uint8_t *rp = src + static_cast<size_t>(r) * vs;
          for (long long c = cLo; c < cHi; ++c) {
            best = std::max(best, static_cast<int>(static_cast<int8_t>(
                                      rp[static_cast<size_t>(c)])));
          }
        }
        const float v = static_cast<float>(best - dcOffset);
        float &cell = blk[static_cast<size_t>(k) * cols + static_cast<size_t>(b)];
        // zone 이 겹치면 더 큰 값을 남긴다(피크 보존).
        cell = std::isnan(cell) ? v : std::max(cell, v);
        minPresent = std::min(minPresent, v);
      }
    }
  }

  // zone 사이 공백(NaN)은 최솟값으로 메우고, 상단 라인용 요약(열별 max-hold)을
  // 같은 패스에서 구한다.
  const size_t bandCols2 = cols;
  std::vector<float> mag(bandCols2);
  for (size_t j = 0; j < bandCols2; ++j) {
    float colMax = -std::numeric_limits<float>::max();
    for (int k = 0; k < n; ++k) {
      float &cell = blk[static_cast<size_t>(k) * bandCols2 + j];
      if (std::isnan(cell)) {
        cell = minPresent;
      }
      colMax = std::max(colMax, cell);
    }
    mag[j] = colMax;
  }

  // 균일 격자이므로 열 j 의 중심 주파수는 선형이다.
  std::vector<float> freqMHz(bandCols2);
  for (size_t j = 0; j < bandCols2; ++j) {
    freqMHz[j] = static_cast<float>(
        (bandLoKHz + (static_cast<double>(j) + 0.5) * gridStepKHz) / 1000.0);
  }

  // --- 드롭 검출 (ICD §1.3, cycleIndex 단조 증가) ---
  if (haveLastCycle_ && cycleIndex > lastCycleIndex_ + 1) {
    dropCount_ += (cycleIndex - lastCycleIndex_ - 1);
  }
  lastCycleIndex_ = cycleIndex;
  haveLastCycle_ = true;
  ++cycleCount_;

  char status[176];
  std::snprintf(status, sizeof(status),
                "cycle #%u  slices %u/%u  %s  rows %d  drops %llu",
                cycleIndex, sliceCount, expectedSlices,
                complete ? "complete" : "partial", n,
                static_cast<unsigned long long>(dropCount_));

  // --- 스냅샷 반영 ---
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.connected = true;
  snapshot_.frequencies = std::move(freqMHz);
  snapshot_.magnitudesDb = std::move(mag);
  snapshot_.spectrogramBlock = block;   // dwell n행 (스펙트로그램에 그대로 투입)
  snapshot_.spectrogramBlockRows = n;
  snapshot_.hmftValid = true; // 광대역 축(주파수 MHz / Level) 사용
  snapshot_.fftFrameCount += 1;
  snapshot_.bytesSent += len;
  snapshot_.packetsSent = cycleCount_;
  snapshot_.status = status;
  snapshot_.error.clear();
}
