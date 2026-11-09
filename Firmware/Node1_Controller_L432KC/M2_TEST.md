# Node 1 M2 CAN 시험

> 아래 내용은 베어메탈 M2 기준의 보존 문서다. 현재 작업 트리는 M3-3 RTOS 구성(`M2_TEST_ENABLED=0`)이며 [M3_TEST.md](M3_TEST.md)를 따른다. M2 재현은 `m2-baseline-before-m3` 또는 보존 ZIP을 별도 디렉터리에 복원한다. Node 1의 최종 PASS는 후속 [M3-2 시험](../../Reports/M3/CAN-Queue-Integration.md)에서 확보했다.

- 시험 구현: `Core/Src/m2_can_test.c`
- 공개 함수와 실행 스위치: `Core/Inc/m2_can_test.h`
- 실행 연결: `Core/Src/main.c`의 USER CODE 영역
- M2 당시 설정: `M2_TEST_ENABLED=1`, CAN1 Normal, 500kbps
- 양쪽 Loopback 정상 출력과 Node 2의 Normal 수신·응답 3,000회 PASS는 사용자가 확인했다. Node 1의 최종 PASS 로그는 재확인이 필요하다.
- [2026-11-07 검증 기록](../../Reports/M2/CAN-Link-Validation.md)

## 실행

1. CubeIDE에서 프로젝트를 Refresh(F5)하고 Build Project를 실행한다.
   새 `m2_can_test.c`와 CAN HAL 소스가 빌드에 포함되는지 확인한다.
2. 보드에 다운로드하고 실행한다. VCP UART는 115200 baud, 8N1이다.
3. LSE/MSI 보정과 주변장치 초기화가 끝나면 시작 로그가 출력된다.
4. 2초 대기 후 20ms마다 3,000프레임을 보내고, 마지막 응답을 위해 1초 더 기다린다.
   시작 로그 기준 약 63초 후 PASS 또는 FAIL 로그를 출력하고 CAN을 정지한다.
5. 재시험하려면 보드를 리셋한다. 측정 중에는 중단점으로 실행을 멈추지 않는다.

정상 로그 예:

```text
[M2] Node 1 CAN test: NORMAL, 500kbps, 3000 frames
[M2] PASS: tx=3000 rx=3000 bad=0 tx_fail=0 slips=0 errors=0 mask=0x00000000 bus_off=0
```

디버거 Expressions에서도 `m2`를 확인할 수 있다.
`done=1`, `passed=1`, `tx_queued=3000`, `rx_ok=3000`이 정상 완료 값이다.
`tx_queued`는 메일박스 등록 횟수이므로 이 값만으로 송신 성공을 판정하지 않는다.
`schedule_slips`는 송신 예정 시각보다 한 주기(20ms) 이상 늦어진 횟수이다.
`hal_error_events`는 HAL 오류 콜백 횟수이며 물리 계층 오류 프레임 개수와 같지는 않다.

## Normal 통신으로 전환

현재 `.ioc`와 `main.c`는 모두 Normal로 전환되어 있으므로 바로 빌드·다운로드한다.
CubeMX의 `Connectivity > CAN1 > Parameter Settings > Operating Mode`에서도
Normal을 확인할 수 있다. 시험 코드는 `hcan1.Init.Mode`에서
모드를 읽으므로 별도 `M2_LOOPBACK` 매크로를 변경할 필요가 없다.

- Node 1 송신: 표준 ID `0x100`, DLC 8.
- 바이트 0~3: 0부터 시작하는 uint32 순번, little-endian.
- 바이트 4~7: `55 AA 12 34`.
- Node 2는 수신한 8바이트를 표준 ID `0x200`으로 그대로 응답해야 한다.
- Node 2를 먼저 실행한 뒤 Node 1을 리셋한다.
- 이 페이로드는 M2 전용이다. 최종 차량 상태/CRC-8 프로토콜이 아니다.

Bus-Off를 감지하면 오류를 기록하고 시험을 종료한다. 자동 복구는 하지 않는다.

## 클럭과 기존 코드

`M2_CAN_Test_PrepareClock()`은 USER CODE SysInit에서 LSE를 켜고 MSI 자동 보정을
활성화한다. 현재 CubeMX 생성 코드에 빠진 부분을 보완하며, 이후 CubeMX에서
LSE 자동 보정을 설정해도 같은 LSE ON 설정과 보정 활성화 호출은 중복 실행 가능하다.
시작 로그 전에 Error_Handler에 멈추면 이 함수의 반환값과 LSERDY를 확인한다.

CAN 자동 재전송과 PA11 RX Pull-up은 `.ioc` 및 생성 코드 양쪽에 반영했다.
CAN 클럭/비트 타이밍이 예상값과 다르면 시험 시작 함수가 HAL_ERROR를 반환한다.

기존 M1 LCD/ADC/버튼 시험은 소스에 남아 있다. `m2_can_test.h`의
`M2_TEST_ENABLED`를 0으로 변경하면 기존 실행 경로를 선택한다.

남은 증빙은 Node 1 최종 PASS 로그와 CAN 비트폭 2µs·MCO 주파수 실측이다. Node 2 로그를 Node 1 결과로 대체하지 않는다.
