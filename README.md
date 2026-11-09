# CAN-Failsafe-Node
Two-node CAN control system on STM32 + FreeRTOS, designed so every failure path converges to a stop — with measured latencies.

## 검증 기록

- [M3-3 조종기 RTOS 이관](Reports/M3/Controller-RTOS-Migration.md): 사용자 보고 기준 양 노드 RTOS 정상 통신·통합 PASS. 3,000회 왕복, 오류·큐 손실·송신 슬롯 누락 0, 조종기 최소 스택 여유 38.28% 이상. [실행 방법](Firmware/Node1_Controller_L432KC/M3_TEST.md).
- [M3-2 CAN RX 큐 이관](Reports/M3/CAN-Queue-Integration.md): 사용자 보고 기준 정상 통신·RTOS 통합 PASS. 양 노드 3,000프레임, 큐 손실·CAN/RTOS 오류 0, 스택 여유 LED 81.25% / Monitor 39.45% / CAN 73.44%. [실행 방법](Firmware/Node2_Vehicle_F446RE/M3_CAN_TEST.md).
- [M3-1 RTOS 기본 구성](Reports/M3/RTOS-Bringup.md): Debug/Release 빌드 통과. 사용자 보고 기준 10분 실행 PASS (`n=668`), LD2 0.5초 점멸, 스택 여유 81%/58%, 오류 카운터 0. 전체 M3 24시간 시험은 남아 있음.
- [M2 CAN 링크 검증 (2026-11-07)](Reports/M2/CAN-Link-Validation.md): 당시 Node 2 수신·응답 3,000회 PASS. Node 1 PASS는 후속 M3-2 시험에서 확보. CAN 비트폭·MCO 주파수 실측은 미완료.
