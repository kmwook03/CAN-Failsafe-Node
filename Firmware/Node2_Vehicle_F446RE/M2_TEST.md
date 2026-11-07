# Node 2 M2 CAN 시험

- 구현: `Core/Src/m2_can_test.c`, `Core/Inc/m2_can_test.h`
- 실행 연결: `Core/Src/main.c`의 USER CODE 영역
- 현재 설정: `M2_TEST_ENABLED=1`, CAN1 Normal, 500kbps
- 양쪽 Loopback 정상 출력과 Node 2의 Normal 수신·응답 3,000회 PASS는 사용자가 확인했다. Node 1 최종 로그 재확인과 계측은 남아 있다.
- [2026-11-07 검증 기록](../../Reports/M2/CAN-Link-Validation.md)
- CAN 핀: PA12 TX(CN10-12), PA11 RX(CN10-14), AF9
- 클럭: 기존 HSE Bypass 8MHz, SYSCLK 160MHz, PCLK1 40MHz
- 타이밍: Prescaler 5, BS1 13TQ, BS2 2TQ, SJW 2TQ
- HAL 드라이버: 설치된 STM32Cube FW_F4 V1.28.3에서 복사

## Loopback

현재는 Normal 설정이다. 아래 단독 시험을 다시 하려면 CubeMX에서 Operating Mode를
Loopback으로 바꾸고 코드를 재생성한 후 실행한다.

1. CubeIDE에서 프로젝트를 Refresh(F5)하고 Build Project를 실행한다.
   `m2_can_test.c`와 `stm32f4xx_hal_can.c`가 빌드되는지 확인한다.
2. 다운로드 후 실행한다. UART는 115200 baud, 8N1이다.
3. 시작 로그 후 2초 대기하고 20ms마다 ID 0x200 프레임 3,000개를 자체 송수신한다.
4. 마지막 송신 후 100ms를 기다리고 CAN을 정지한다. 시작 로그 기준 약 62.1초이다.

정상 로그 예:

```text
[M2] Node 2 CAN test: LOOPBACK, 500kbps, 3000 frames
[M2] PASS: tx=3000 completed=3000 rx=3000 bad=0 tx_fail=0 slips=0 errors=0 mask=0x00000000 bus_off=0 timeout=0
```

디버거 Expressions의 `m2`에서도 결과를 확인할 수 있다.
`done=1`, `passed=1`, `tx_queued=tx_completed=rx_total=rx_ok=3000`이 정상이다.
`tx_queued`는 메일박스 등록, `tx_completed`는 HAL 송신 완료 콜백 횟수이다.
시험 중 중단점으로 실행을 멈추면 주기 검사에 실패할 수 있다.

## Node 1과 Normal 왕복 통신

양쪽 `.ioc`와 `main.c`는 이미 Normal로 전환되어 있으므로 빌드·다운로드한다.
CubeMX의 `Connectivity > CAN1 > Parameter Settings > Operating Mode`에서도 확인할 수 있다.
시험 코드는 `hcan1.Init.Mode`를 읽으므로 별도 모드 매크로는 없다.

1. CANH-CANH, CANL-CANL, 공통 GND와 양 끝 120옴 종단을 연결한다.
2. Node 2를 먼저 실행하고 다음 로그를 확인한다.
   `[M2] Node 2 CAN test: NORMAL ECHO, 500kbps, 3000 frames`
3. Node 1을 리셋한다. Node 2는 첫 프레임을 받을 때까지 계속 기다린다.
4. Node 1이 보낸 표준 데이터 ID 0x100/DLC 8을 검사하고,
   수신한 8바이트를 그대로 ID 0x200으로 응답한다. 모터 제어는 하지 않는다.
5. 양쪽 PASS를 확인한다. Node 1의 `rx_ok=3000`이 왕복 데이터 검증의 근거이다.

페이로드: 바이트 0~3은 0부터 증가하는 uint32 little-endian 순번,
바이트 4~7은 `55 AA 12 34`이다. 최종 차량 상태/CRC-8 형식이 아닌 M2 시험 형식이다.
길이가 정상인 잘못된 페이로드도 에코하여 Node 1에서 함께 검출하도록 한다.

첫 프레임 이후 1초 동안 추가 수신이 없거나 시험이 65초를 넘으면 timeout FAIL이다.
3,000프레임 수신 후에는 마지막 수신부터 100ms 뒤 결과를 확정한다.
이는 Node 1의 마지막 응답 대기 시간(1초)보다 짧으며, 정상 응답 송신에는 충분한 여유이다.
Bus-Off 발생 시 오류를 기록하고 종료하며 자동 복구하지 않는다.
두 노드를 모두 리셋하면 다시 시험할 수 있다.

## 기존 시험과 코드 재생성

`m2_can_test.h`의 `M2_TEST_ENABLED`를 0으로 바꾸면 기존 초음파 시험을 실행한다.
M2에서는 초음파 시험 시작과 모터 PWM 시작을 호출하지 않는다.
별도 시험 파일은 Core 소스 경로에 있으며, main의 연결은 USER CODE 영역에 있다.
CAN 초기화, GPIO, NVIC와 자동 재전송은 `.ioc`에도 반영했다.
Node 2는 송신 완료 횟수 검증을 위해 RX0/SCE 외에 **CAN1 TX 인터럽트**도 사용한다.

Normal 시험의 Node 2 PASS는 사용자 로그로 확보했다. Node 1 최종 로그와 CAN 비트폭 2µs·MCO 주파수는 별도로 확인해야 한다.
