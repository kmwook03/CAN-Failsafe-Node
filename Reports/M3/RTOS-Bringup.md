# M3-1 RTOS 기본 구성 기록

작업일: 2026-11-09 (Asia/Seoul)

**M3-1 초기 10분 동작 시험: PASS (사용자 보드 실행 보고 기준).**

차량 노드 F446RE에 FreeRTOS V10.3.1/CMSIS-RTOS v2, TIM6 HAL timebase, LED 태스크와 UART 진단 태스크를 추가했다. 이 단계는 RTOS 실행 기반 구성이다. CAN RX 큐와 주변장치 태스크 이관은 아직 구현하지 않았다.

## 검증 범위

- CubeMX 6.18.1과 STM32CubeF4 V1.28.3으로 생성했다.
- STM32CubeIDE 2.2.0, Arm GNU Toolchain 14.3.rel1로 Debug/Release 빌드 모두 오류 0건·경고 0건을 확인했다. 최종 결과와 바이너리 해시는 `evidence/build-summary.json`, 컴파일 로그는 `evidence/build.log`에 기록했다.
- 최종 ELF의 벡터 테이블을 직접 읽어 SVC/PendSV/SysTick/TIM6가 실제 핸들러 심벌에 연결되어 있음을 확인했다. HAL_InitTick 재정의와 두 M3 태스크의 링크도 확인했다. CubeMX 재생성 후 태스크 진입점·오류 훅·LED 호환 정의·CAN 자동 재전송 설정이 유지됐다.
- 사용자가 10분 실행 후 최종 UART 로그와 LD2의 0.5초 점멸을 보고했다. tick 증가와 스택 여유는 초기 기준을 충족하고 오류 카운터는 모두 0이다. 이 보고를 근거로 M3-1 초기 동작 시험을 PASS로 판정했다. 에이전트의 직접 계측 결과는 아니며 24시간 안정성은 미검증이다.
- M2 기준점은 로컬 태그 `m2-baseline-before-m3` (`1af032e7cd35b84679f8cfc7cc024b16b4e9cdb3`)로 보존되어 있다. Node 1 코드는 수정하지 않았다.

| 구성 | text | data | bss | 빌드 결과 |
|---|---:|---:|---:|---|
| Debug | 32,408 B | 104 B | 21,080 B | 오류 0 / 경고 0 |
| Release | 19,040 B | 104 B | 21,072 B | 오류 0 / 경고 0 |

위 크기는 ELF 섹션 크기이며 실행 중 태스크의 최대 스택 사용량을 뜻하지 않는다.

## 사용자 보드 실행 보고

2026-11-09에 전달된 값이다. [원문 및 판정 범위](evidence/user-reported-uart.txt)를 별도 보존했다.

| 항목 | 사용자 전달 값 | 판정 |
|---|---|---|
| 진단/LED 카운터 | `n=668 led=1337` | 약 668초 실행과 0.5초 전환 주기에 부합 |
| RTOS/HAL 증가량 | `dt=1000/1000` | 전달 표본에서 일치 |
| LED 태스크 스택 여유 | 832 / 1,024 B = 81.25% | 30% 기준 충족 |
| 진단 태스크 스택 여유 | 1,192 / 2,048 B ≈ 58.20% | 30% 기준 충족 |
| RTOS 최소 힙 여유 | 12,048 B | 최초 표본과 동일 |
| 오류 카운터 | `tickErr=0 late=0/0 uartErr=0` | 10분 실행 후에도 모두 0 |
| LED | LD2가 0.5초마다 점멸 | 사용자 관찰로 확인 |

최종 전달 로그:

```text
[M3] n=668 led=1337 dt=1000/1000 stackB=832/1192 heapMinB=12048 tickErr=0 late=0/0 uartErr=0
```

`n=668`은 1초 진단 주기에서 약 11분 8초에 해당한다. `led=1337`은 시작 직후 전환 1회와 이후 초당 2회 전환에 부합한다. 두 태스크의 최소 스택 여유와 RTOS 최소 힙 여유는 최초 표본과 같다. 사용자 실행 보고와 최종 누적 카운터를 근거로 초기 시험을 통과로 기록한다. 원본 연속 로그와 별도 reset/HardFault 이력 계측은 없으므로 전체 M3의 24시간 무정지 검증으로 확대하지 않는다.

다음 단계는 F446RE의 CAN 수신 ISR에서 프레임을 큐로 넘기고 `CanRxTask`에서 검증·에코를 수행하도록 이관한 뒤, M2의 3,000프레임 왕복 시험을 다시 수행하는 것이다.

## 구현 내용

`ledTask`는 500tick마다 LD2를 전환한다. 우선순위가 낮은 `monitorTask`는 1,000tick마다 RTOS/HAL 시간 증가량, LED 실행 횟수, 태스크별 최소 스택 여유, RTOS 최소 힙 여유와 오류 카운터를 출력한다. 스택 오버플로·할당 실패·assert는 디버거로 확인할 수 있는 중단 원인을 남긴다.

SysTick은 CMSIS-RTOS2 래퍼를 통해 RTOS tick을 처리하고, TIM6 인터럽트는 HAL tick만 증가시킨다. SVC와 PendSV는 FreeRTOS Cortex-M4F 포트를 사용한다. HAL 초기화가 설정하는 PriorityGroup 4를 유지하고 RTOS API 호출이 가능한 CAN/TIM4 IRQ 우선순위를 5로 지정했다.

## 생성 과정에서 확인한 문제

1. 첫 CubeIDE 프로젝트 설정 갱신에서 기존 빌드 구성을 찾지 못하는 도구 오류가 발생했다. 후속 생성 결과와 `.cproject`를 확인하고 Debug/Release의 미들웨어 소스·include 경로를 반영했다.
2. BSP LED를 사용하는 생성 코드가 기존 `LD2_Pin`/`LD2_GPIO_Port` 정의를 제거하여 미사용 M1 초음파 소스도 컴파일하지 못했다. USER CODE 영역에 BSP 이름을 사용하는 호환 정의를 넣었다.
3. 기존 `CAN1.AutoRetransmission` IOC 키가 생성기에 반영되지 않아 자동 재전송이 꺼지는 것을 확인했다. 데이터베이스의 실제 키 `CAN1.NART=ENABLE`로 수정하여 기존 M2 설정을 유지했다.

보드 실행 절차와 초기 합격 기준은 [M3_TEST.md](../../Firmware/Node2_Vehicle_F446RE/M3_TEST.md)에 정리했다. M3-1 초기 시험은 통과했으며 전체 M3의 태스크 통합과 24시간 시험은 남아 있다.
