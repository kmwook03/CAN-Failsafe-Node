# Node 1 조종기 RTOS 이관 시험 (M3-3)

조종기 L432KC의 M2 CAN 왕복 시험을 FreeRTOS 태스크로 이관했다. **2026-11-09 사용자 보고 기준 M3-3 정상 통신·RTOS 통합 시험 PASS다.** 양 노드 3,000회 왕복, 오류·손실 0, 모든 태스크의 최소 스택 여유 30% 이상을 확인했다. 아래 절차는 재시험에도 적용한다. 조이스틱 ADC, 버튼 제어, LCD의 애플리케이션 태스크 통합은 후속 작업이다.

## 실행 순서

1. CubeIDE에서 `Node1_Controller_L432KC`를 Refresh(F5)하고 Debug 구성으로 Build한다. `M2_TEST_ENABLED=0`을 유지한다.
2. 조종기 L432KC에 새 펌웨어를 다운로드한다. 차량 F446RE에는 M3-2에서 통과한 펌웨어를 사용한다.
3. 양 보드 UART를 115200 baud, 8N1로 연다. 기존 CAN 배선·종단·공통 접지는 유지한다.
4. **차량부터 리셋하고 `[M3-CAN] READY`를 확인한 뒤 조종기를 리셋한다.** 다운로드 과정에서 먼저 실행된 시험 결과는 무시하고 이 순서로 다시 시작한다.
5. 조종기는 스케줄러 시작 후 2초 대기하고, 20ms마다 3,000프레임을 전송한다. 마지막 송신 후 1초간 응답을 기다린다. 조종기 시작 배너부터 약 63초 후 최종 결과가 나온다. 측정 중 중단점으로 실행을 멈추지 않는다.
6. CAN 결과 뒤에도 5초 이상 계속 실행하고 양 보드의 RTOS 진단을 수집한다. 태스크가 최종 결과 출력까지 수행한 뒤의 최소 스택 여유를 확인하기 위해서다.
7. 재시험은 매번 차량 → 조종기 순서로 둘 다 리셋한다. CAN 태스크는 시험 종료 후 suspend 상태로 남고, LED·진단 태스크는 계속 실행된다.

## 수집할 출력과 합격 기준

| 보드 | 수집할 줄 | 기준 |
|---|---|---|
| 조종기 | `[M3-N1-CAN] PASS` | `tx=completed=rx=3000`, `bad`, `tx_fail`, `slips`, `errors`, `mask`, `bus_off`, `timeout`, `initErr`, `stopErr` 모두 0 |
| 조종기 | `[M3-N1-CAN] queue` | `received=enqueued=processed=3000`, `dropped=readErr=qErr=0`, `peak<=16` |
| 조종기 | `[M3-N1-CAN] timing` | `periodMs=20`, `deadlineErr=0`; `maxLateTicks`는 관측값으로 보존 |
| 조종기 | CAN 결과 출력 이후 `[M3-N1]` | `dt` 약 `1000/1000`, `tickErr=0 late=0/0 uartErr=0`; 최소 스택 여유 기준 충족 |
| 차량 | `[M3-CAN] PASS`, `queue` | 기존 M3-2 기준과 동일: 모든 송수신·처리 3,000회, 오류·손실 0 |
| 차량 | CAN 결과 출력 이후 `[M3]` | 기존 M3-2 기준과 동일: 오류 0, 최소 스택 여유 30% 이상 |

조종기의 `stackB`는 LED / Monitor 순서이며 각각 1,024 / 2,048 B를 할당했다. `txStackB`, `rxStackB`는 각각 1,024 B 중 지금까지 한 번도 사용하지 않은 최소 여유다. **LED/TX/RX는 각각 308 B 이상, Monitor는 615 B 이상**이어야 30% 여유 기준을 충족한다. `heapMinB`는 실행 중 관측한 최소 힙 여유다. 조종기 녹색 LED는 0.5초마다 상태가 바뀌어야 한다.

`maxLateTicks`는 송신 태스크가 예정 시각보다 늦게 실행된 정도이며 1tick=1ms다. 20tick 이상 지연된 슬롯은 `slips`에 누적하고 다음 송신 시각을 재설정한다. 큐의 `maxWaitMs`는 ISR 수신 시각부터 태스크 처리까지 HAL tick으로 측정한 최대값이다. 둘 다 정밀한 CAN 버스 전송 시각 측정이나 최악 지연 상한 보장이 아니다.

CAN 자체의 PASS만으로 M3-3 전체 합격을 판정하지 않는다. 위 RTOS 상태와 스택 여유를 함께 확인한다. 24시간 연속 시험과 Bus-Off·큐 포화 오류 주입은 별도다.

## 문제가 생기면

- `[M3-N1-CAN] FAIL`: 해당 줄과 queue/timing, 직후 `[M3-N1]` 진단을 함께 보존한다. `initErr`는 CAN 시작, `tx_fail`은 송신 등록, `completed` 부족은 실제 전송 완료, `rx` 부족은 에코 검증 문제를 구분한다.
- 출력·LED가 모두 멈추면 디버거에서 `m3.fatal_reason`을 확인한다. 1=assert, 2=stack overflow, 3=allocation, 4=kernel/API, 5=HAL 초기화 오류다. 클록 초기화에서 실패하면 UART 초기화 전이므로 배너가 없을 수 있다.
- 베어메탈 M2로 돌아가려면 단일 스위치를 바꾸지 말고 `m2-baseline-before-m3` 또는 보존 ZIP을 별도 디렉터리에 복원한다. 현재 프로젝트의 main과 인터럽트는 RTOS 구성이다.

구현과 빌드 증빙: [Controller-RTOS-Migration.md](../../Reports/M3/Controller-RTOS-Migration.md).
