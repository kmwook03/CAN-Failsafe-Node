# M3-2: CAN RX 큐와 태스크 이관 시험

차량(F446RE)은 현재 M3 펌웨어를 사용하고, 조종기(L432KC)는 기존 M2 Normal 송신 펌웨어를 사용한다. Node 1의 RTOS 이관은 후속 작업이다. 모터 PWM과 초음파 측정은 시작하지 않는다.

## 수신 경로

```text
CAN RX0 ISR
  → 헤더·8바이트 데이터·HAL 도착 시각을 큐에 복사
  → canRxTask: 형식·순번·페이로드 검증
  → ID 0x200으로 원문 에코
  → TX 완료 ISR: 완료 횟수 기록
  → monitorTask: UART 진행/최종 결과 출력
```

큐는 16개 슬롯이며 커널 초기화 후, 스케줄러 시작 전에 생성한다. CAN 시작과 알림 활성화는 스케줄러가 실행한 `canRxTask`에서 수행한다. 수신 ISR은 한 번에 최대 3프레임을 읽고 timeout=0으로 큐에 넣는다. 프레임을 값으로 복사하므로 ISR 지역변수의 포인터가 큐에 남지 않는다. 파싱·송신·UART 출력은 RX ISR에서 수행하지 않는다.

| 항목 | 설정 |
|---|---|
| `canRxTask` | CMSIS AboveNormal(32), 스택 1,024바이트 |
| 큐 대기 | 프레임 도착 시 실행, 최대 10tick 대기 후 종료/오류 조건 확인 |
| `ledTask` | Normal(24), 스택 1,024바이트, 500tick 주기 |
| `monitorTask` | Low(8), 스택 2,048바이트, 1,000tick 주기 |
| 시간 기준 | SysTick RTOS 1kHz, TIM6 HAL 1ms |
| ISR 우선순위 | CAN RX0/TX/SCE = 5 |
| 시험 프레임 | Node 1 → `0x100`, Node 2 → `0x200`, 표준 데이터 프레임, DLC 8 |
| 페이로드 | Byte 0~3: 순번 0~2999 little-endian, Byte 4~7: `55 AA 12 34` |

큐 API 사용은 [CMSIS-RTOS2 공식 문서](https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__Message.html)의 ISR timeout=0 조건을 따른다. 설치된 ST 래퍼가 내부에서 `xQueueSendToBackFromISR()`와 필요한 컨텍스트 전환을 수행한다.

## 실행 순서

1. CubeIDE에서 **Node2_Vehicle_F446RE**를 Refresh하고 Debug 구성을 빌드·다운로드한다. 디버거가 `main`에서 정지하면 Resume한다.
2. 두 보드의 CAN 배선을 기존 M2와 동일하게 유지한다. 각 UART는 115200 baud, 8N1, 흐름 제어 없음으로 연다. 노드별 로그를 별도 파일로 저장한다.
3. 차량 UART에서 다음 READY 로그를 기다린다. 첫 프레임이 오기 전에는 WAIT 상태로 계속 기다린다.

   ```text
   [M3-CAN] READY: NORMAL ECHO, 500kbps, 3000 frames; reset Node 1
   ```

4. **그 다음 Node 1을 리셋**한다. Node 1에 M2 펌웨어가 없다면 기존 소스를 빌드·다운로드한 뒤 같은 순서로 시작한다. 이번 단계에서 Node 1의 IOC나 코드는 변경하지 않는다.
5. 중단점을 사용하지 않고 약 63초 기다린다. 차량에서는 기존 `[M3]` 진단이 매초, CAN 진행 상황이 5초마다 출력된다. 마지막 수신 뒤 100ms 동안 마지막 송신 완료를 기다린 후 CAN을 정지하고 결과를 고정한다. LED와 RTOS 진단은 계속 실행된다.
6. 차량의 최종 `[M3-CAN] PASS/FAIL` 줄과 이어지는 `queue:` 줄, 최신 `[M3]` 줄, 조종기의 최종 `[M2] PASS/FAIL` 줄을 함께 보존한다.

두 노드 모두 재시험하려면 다시 시작해야 한다. 이미 시험을 끝낸 차량의 CAN은 정지되어 있으므로 Node 1만 리셋하면 새 시험이 되지 않는다.

## 합격 기준

아래 출력은 기대 형식이며 실측 결과가 아니다.

```text
[M3-CAN] PASS: tx=3000 completed=3000 rx=3000 bad=0 tx_fail=0 errors=0 mask=0x00000000 bus_off=0 timeout=0 initErr=0 stopErr=0
[M3-CAN] queue: received=3000 enqueued=3000 processed=3000 dropped=0 readErr=0 qErr=0 peak=<관측값> maxWaitMs=<관측값> stackB=<관측값>
```

| 항목 | 기준 |
|---|---|
| 차량 CAN 결과 | PASS, 수신·에코 등록·송신 완료 각각 3,000회, 오류 0 |
| 큐 경로 | `received=enqueued=processed=3000`, `dropped=readErr=qErr=0` |
| 조종기 검증 | `[M2] PASS`, `tx=rx=3000`, bad/tx_fail/slips/errors/bus_off 모두 0 |
| RTOS 진단 | `tickErr=0`, `late=0/0`, `uartErr=0`, `dt` 약 `1000/1000` |
| 태스크 스택 | LED와 CAN 최소 여유 각각 308바이트 이상, Monitor 615바이트 이상 |
| LED | LD2가 0.5초마다 계속 전환 |

기존 `[M3]` 줄의 `stackB`는 LED/Monitor 두 값이고, 새 `canStackB`는 CAN 태스크 값이다. CAN 결과의 `queue: ... stackB`도 CAN 태스크의 최소 스택 여유를 뜻한다. CAN 태스크가 종료 후 정지 상태로 남아 있어 최종 스택 값을 확인할 수 있다. 모니터의 CAN 결과 포맷팅 이후까지 반영하려면 PASS 뒤 2~3개의 `[M3]` 줄도 확인한다.

`peak`는 큐 최대 점유량, `maxWaitMs`는 HAL tick으로 계산한 ISR 수신부터 태스크 처리까지의 최대 대기 시간이다. 1ms 단위이며 정밀 응답시간 측정을 대체하지 않는다. 새 태스크와 큐가 추가되므로 힙·스택 여유는 M3-1에서 보고한 값과 달라질 수 있다.

## 실패 해석

- READY 대신 FAIL과 `initErr=1`: CAN 설정/시작 실패. `hcan1.ErrorCode`와 클럭·CAN Normal 설정을 확인한다.
- WAIT만 계속됨: Node 1 시작 여부와 CAN 배선을 확인한다. 첫 수신 전에는 타임아웃으로 종료하지 않는다.
- 일부 수신 후 `timeout=1`: 마지막 도착 시각부터 1초 경과 또는 첫 수신부터 65초 경과. 큐에서 1초 이상 묵은 프레임도 에코하지 않고 실패 처리한다.
- `dropped>0`: 큐 삽입 실패. 손실 프레임을 숨기지 않고 시험을 실패로 종료한다.
- `received/enqueued`와 `processed` 불일치: 처리하지 못한 프레임이 남아 있는 실패 결과. 모든 값이 3,000이어야 PASS다.
- `tx=3000`이나 `completed<3000`: 송신 요청과 실제 완료가 다르므로 실패다.
- `bad>0`: 헤더·길이·순번·페이로드 불일치. 헤더와 길이가 정상인 손상 페이로드는 Node 1도 검출할 수 있도록 원문 그대로 에코한다.
- `bus_off=1`: 오류 기록 후 시험 종료. 자동 복구는 이번 단계에 포함하지 않는다.

이 펌웨어의 PASS는 CAN 경로 시험 결과다. RTOS 진단과 스택 여유도 함께 확인해야 M3-2 통합 시험을 통과로 판정한다. M3 전체의 양 노드 태스크 통합·24시간 시험은 별도다.
