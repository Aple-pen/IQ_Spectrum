# BinTcpSpectrum

Windows용 CMake 기반 ImGui 애플리케이션입니다. `.bin` 파일을 읽어 TCP 대상 IP/Port로 전송하고, 전송한 바이트를 FFT 처리해 실시간 스펙트럼으로 표시합니다.

## Features

- ImGui + GLFW + OpenGL3 UI
- ImPlot 기반 실시간 spectrum chart
- TCP server bind IP/port 설정
- FFT size 설정
- 전송 chunk size 설정
- sample format 선택: `uint8`, `int16 little-endian`, `float32 little-endian`
- loop, send interval, Start/Stop 제어

## Build

Visual Studio 개발자 PowerShell 또는 VS Code CMake Tools에서 실행하세요.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

실행 파일:

```powershell
.\build\Release\BinTcpSpectrum.exe
```

처음 configure 시 CMake `FetchContent`가 GLFW, ImGui, ImPlot을 GitHub에서 내려받습니다.

## Notes

- 현재 TCP 동작은 앱이 TCP 서버로 `listen`하고 클라이언트 접속 시 `.bin` 데이터를 송신하는 방식입니다.
- 기본 bind IP는 `0.0.0.0`이며, 동일 PC에서 테스트할 때는 클라이언트가 `127.0.0.1:포트`로 접속하면 됩니다.
- 실제 ADC/IQ 데이터라면 sample format, channel count, endian, scale factor를 장비 포맷에 맞게 확장하는 것을 추천합니다.