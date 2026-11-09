# M3-1: F446RE RTOS 기본 동작 시험

> 이 문서는 10분 동작 시험을 통과한 M3-1 구성의 기록이다. 현재 작업 버전은 CAN RX 큐를 추가한 M3-2이며, 실행 방법은 [M3_CAN_TEST.md](M3_CAN_TEST.md)를 따른다.

M3-1은 차량 노드의 스케줄러와 두 시간 기준을 확인했다. 당시 펌웨어는 CAN 초기화까지만 수행하고 CAN 통신과 모터 PWM을 시작하지 않았다. Node 1은 이 단독 시험에 필요하지 않다. 센서 통합, 모터 제어, 24시간 시험은 후속 단계다.

## 설정과 코드

| 항목 | 설정 |
|---|---|
| 소스 기준 | `m2-baseline-before-m3` (`1af032e`) |
| 미들웨어 | STM32CubeF4 V1.28.3에 포함된 FreeRTOS V10.3.1 + CMSIS-RTOS v2 |
| RTOS tick | SysTick, 1,000Hz |
| HAL tick | TIM6, 1ms; 최종 160MHz 클럭 설정에서 PSC=79, ARR=999 |
| NVIC | PriorityGroup 4; CAN/TIM4 설정 우선순위 5, PendSV/SysTick/TIM6 우선순위 15 |
| `ledTask` | Normal, 스택 1,024바이트, 500tick마다 LED 전환 |
| `monitorTask` | Low, 스택 2,048바이트, 1,000tick마다 UART 진단 |
| RTOS 힙 | heap_4, 15,360바이트 |
| 오류 감지 | configASSERT, 스택 오버플로 검사 2, malloc 실패 훅 |

`main.c`의 CubeMX 태스크 진입점은 `m3_rtos_test.c`를 호출한다. 주기 대기는 `osDelayUntil()`을 사용한다. 진단 태스크만 UART에 쓰며 전송 타임아웃은 100ms다. 두 tick의 부팅 시점이 다르므로 절대값이 아닌 1초 구간 증가량을 비교한다. tick 간 차이가 5ms를 넘으면 `tickErr`를 누적한다. 이는 물리적인 클럭 정밀도 측정을 대체하지 않는다.

`freertos.c`의 오류 훅은 UART 출력이나 메모리 할당 없이 중단하고 `m3.fatal_reason`을 남긴다. Cortex HardFault는 별도의 `HardFault_Handler`에서 중단한다.

## 보드에서 실행

1. CubeIDE에서 차량 프로젝트를 Refresh(F5)하고 Debug 구성을 빌드한다. 이미 열린 IDE가 외부 `.cproject` 변경을 반영하지 않으면 프로젝트를 닫았다 다시 연다.
2. F446RE를 USB로 연결하고 해당 프로젝트를 다운로드한다. Debug 실행 시 `main`에 멈추면 Resume한다.
3. 차량 보드의 VCP UART를 **115200 baud, 8N1, 흐름 제어 없음**으로 연다. 터미널의 파일 저장을 시작하고 보드를 리셋하면 시작 배너도 보존할 수 있다.
4. LD2가 0.5초마다 켜짐/꺼짐을 전환하는지 확인한다. 완전한 점멸 주기는 1초다.
5. 중단점 없이 10분간 진단 로그를 확인한다. 다음은 출력 형식이며 측정 결과가 아니다.

```text
[M3] Node 2 RTOS bring-up: FreeRTOS V10.3.1, CMSIS-RTOS2, SysTick=1kHz, HAL=TIM6
[M3] n=<표본수> led=<전환수> dt=<RTOS tick 증가>/<HAL ms 증가> stackB=<LED>/<Monitor> heapMinB=<최소 힙 여유> tickErr=<횟수> late=<LED>/<Monitor> uartErr=<횟수>
```

| 확인 항목 | 초기 합격 기준 |
|---|---|
| 주기 실행 | `n`이 초당 1씩, `led`가 대략 2씩 증가 |
| 시간 기준 | `dt`가 약 `1000/1000`, `tickErr=0` |
| 지연/전송 | `late=0/0`, `uartErr=0` |
| 스택 여유 | 두 번째 표본 이후 `stackB`가 LED 308바이트 이상, Monitor 615바이트 이상 |
| 메모리 | `heapMinB`가 0보다 크고 안정적으로 유지 |
| 실행 상태 | 리셋·멈춤 없음, 디버거의 `m3.fatal_reason=0` |

스택 값은 실행 이력에서 관찰된 최소 여유다. 첫 번째 진단은 첫 `snprintf()` 실행 전 값을 표시하므로 두 번째 이후를 확인한다. 이 초기 시험만으로 전체 M3의 스택 여유나 24시간 안정성을 판정하지 않는다. 디버거 중단 중에는 HAL/RTOS 시간이 서로 다르게 진행할 수 있으므로, 중단점을 사용한 후에는 리셋하여 다시 측정한다.

로그가 없으면 `main`, `M3_RtosFatal`, `HardFault_Handler` 중 어디에서 멈추는지 확인한다. `m3.fatal_reason`은 1=assert, 2=스택 오버플로, 3=메모리 할당, 4=커널/태스크 시작, 5=HAL 초기화 오류다.

## 코드 재생성 및 M2 복원

- `.ioc`에 CMSIS_V2, 두 태스크, TIM6 timebase, IRQ 우선순위와 메모리 검사 설정을 저장했다. 사용자 로직은 별도 파일과 USER CODE 영역에 있다.
- 재생성 후 Debug/Release 모두 `Middlewares`가 소스 경로에 포함되어야 한다. include 경로에는 FreeRTOS의 `Source/include`, `Source/CMSIS_RTOS_V2`, `Source/portable/GCC/ARM_CM4F`가 필요하다.
- 기존 CAN 자동 재전송 설정의 IOC 항목을 CubeMX가 인식하는 `CAN1.NART=ENABLE`로 바로잡았다. 생성 코드의 `hcan1.Init.AutoRetransmission`은 `ENABLE`을 유지해야 한다.
- 기존 초음파 시험에 필요한 LD2 이름은 `main.h`의 USER CODE 영역에서 BSP 이름으로 연결했다.
- M2 시험 코드는 보존되어 있으나 현재 진입점에서 호출하지 않는다. 원래 M2를 재시험하려면 `m2-baseline-before-m3` 보존본을 별도 폴더에서 빌드한다. `M2_TEST_ENABLED`만 바꿔서는 베어메탈 진입점이 복원되지 않는다.

보드 결과는 `Reports/M3/evidence`에 날짜·보드·COM 포트·펌웨어 해시와 함께 추가한다. 현재 빌드 기록은 [M3-1 구현·검증 기록](../../Reports/M3/RTOS-Bringup.md)을 참조한다.
