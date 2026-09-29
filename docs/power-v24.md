# v24 오른쪽 대기 전력 수정

2026-09-29 사용자 확인: 양쪽 v23, 전날 17시부터 사용하지 않았는데 오른쪽 잔량 95% → 54%. 보고된 잔량은 41%p 하락이며, 전류·배터리 용량을 직접 측정한 값은 아닙니다. 아래 원인은 코드 비교로 확인한 결함/설정 차이이고 실제 소모에 대한 각 항목의 기여도는 아직 측정하지 않았습니다.

| 항목 | 원본 / ZMK v0.3 | v23 | v24 |
| --- | --- | --- | --- |
| 오른쪽→왼쪽 BLE 대기 이벤트 건너뛰기 | latency 기본 30 | 왼쪽 설정에서 0으로 강제 | 30 복구 |
| 키보드 사이 기본 연결 간격 | 6 × 1.25ms | 동일 | 동일 |
| 센서 절전 처리 | 원본 드라이버 직접 호출 | 별도 큐에 예약 후 즉시 반환 | 완료까지 대기, 이전 깨우기 작업 정리 |
| 센서의 기기 전원 관리 등록 | 없음 | 없음 | suspend/resume 등록, 실패 전파 |
| 표시용 ABS가 활동 시간 갱신 | 원본에 해당 표시 데이터 없음 | 모든 표시 데이터가 활동으로 취급 | 전용 코드 0x18, 0x30~0x33만 활동에서 제외 |
| 오른쪽 USB 센서 진단 | 없음 | 비활성 | 비활성 유지 |
| 대기 / 깊은 절전 | 30초 / 60분 | 동일 | 동일 |

□ BLE: 현재 왼쪽의 CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=0은 오른쪽이 사용하지 않아도 매 연결 이벤트에 참여하도록 요구합니다. 원본이 쓰는 ZMK 기본값 30을 복구합니다. 최대 30회 건너뛰기를 허용한다는 뜻이며 31배 배터리 수명을 의미하지 않습니다. 데이터가 생기면 주변 장치가 더 일찍 참여할 수 있습니다. PC 출력의 별도 15ms/latency 0과 기존 마우스 송신 코드는 유지합니다. 실제 협상 결과 및 사용 중 지연은 장치 적용 후 확인 대상입니다.

□ 절전 순서: ZMK는 SLEEP 활동 이벤트를 올린 뒤 기기를 suspend하고 sys_poweroff를 호출합니다. v23의 센서 sleep은 큐에 등록만 하므로, 센서에 절전 명령을 쓰기 전에 I2C나 MCU가 꺼질 수 있었습니다. 센서 절전 완료를 기다리고 Zephyr PM에도 등록했습니다. 센서보다 버스가 먼저 정지하는 순서 문제와 실패 누락을 막습니다. I2C 레지스터 읽기 실패 후 0을 써 버리거나, 쓰기 NACK를 이미 잠들었다고 간주하는 분기도 수정했습니다. 통신 종료 주소의 정상 NACK 처리와 기동 대기는 유지합니다.

□ 활동 시간: sync=false는 HID 마우스 보고만 막습니다. ZMK v0.3의 전체 input callback은 sync에 관계없이 활동 시간을 갱신합니다. 따라서 배터리/USB/설정값/강도 표시도 잠들기 전 시간을 다시 시작시켰습니다. 원본 activity.c를 빌드 때 복사하고 이 callback 한 곳만 필터링합니다. 예상한 callback이 없으면 빌드를 실패시켜 업스트림 변경을 놓치지 않습니다. 키, 접촉 시작/종료, 이동/스크롤/버튼과 다른 ABS 입력은 계속 활동입니다.

□ v23 충전 상태 전송은 변경마다 최대 세 패킷이며 계속 도는 heartbeat는 아닙니다. 이것만으로 밤새 절전이 불가능했다고 단정하지 않습니다. 센서의 8ms 설정은 접촉 중 보고 주기이며 손이 없을 때 무조건 125Hz 전송한다는 뜻도 아닙니다. 화면의 250ms 확인은 왼쪽 로컬 캐시 조회입니다. 이 세 항목을 무조건 상시 전력 원인으로 취급하지 않습니다.

□ 검사: 실제 전원 전환 함수와 활동 callback을 호스트 검사로 컴파일하여 절전 완료 후 반환, 예약/실행 중 깨우기 정리, 중복 suspend, resume, 버스·GPIO·잠금 오류, 표시 데이터와 실제 입력 구분을 확인합니다. 기존 클릭·전송·LCD 검사도 실행합니다. 물리적인 I2C 전압이나 전류를 측정하는 검사는 아닙니다. 빌드 결과와 산출물 검증은 FIRMWARE_VERSIONS.md에 기록합니다.

□ 적용 후 확인: 양쪽 v24를 적용하고 USB를 분리한 상태에서 사용 종료 시각·잔량과 다음 사용 전 시각·잔량을 비교합니다. USB가 연결되면 원래 ZMK 정책상 깊은 절전이 차단되므로 같은 조건의 방치 비교가 되지 않습니다. 저장값 초기화는 하지 않습니다.

출처: [1](https://github.com/beekeeb/zmk-keyboard-toucan2/blob/main/config/toucan_left.conf) 원본 키보드 설정, [2](https://github.com/beekeeb/zmk_driver_azoteq/blob/main/drivers/input/tps43.c) 원본 센서 드라이버, [3](https://github.com/zmkfirmware/zmk/blob/v0.3/app/src/split/bluetooth/Kconfig) split 기본값, [4](https://github.com/zmkfirmware/zmk/blob/v0.3/app/src/activity.c) 활동/절전, [5](https://github.com/zmkfirmware/zmk/blob/v0.3/app/src/pm.c) 기기 suspend 순서, [6](https://www.azoteq.com/images/stories/pdf/iqs5xx-b000_trackpad_datasheet.pdf) 센서 suspend 및 I2C 7.3/8절, [7](https://academy.nordicsemi.com/courses/designing-low-power-bluetooth-le-products/lessons/lesson-4-bluetooth-le-power-optimization/topic/bluetooth-le-connection-parameters-and-power-consumption/) BLE 연결 매개변수와 전력. 원본 main은 2026-09-29 조회 기준입니다.
