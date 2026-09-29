# HID 창 최소화

현재 기능은 호스트 지원을 확인하기 위한 AC Minimize 직접 전송입니다.

□ 기존 X+C 콤보와 `&WINMINIMIZE` 비헤이비어를 유지합니다. 전송 내용은 Consumer Page 0x0C / Usage 0x0206 한 개입니다. Alt, Space, N, Windows 키는 전송하지 않습니다.

□ 키맵에 `C_MINIMIZE`를 정의했습니다. 다른 위치에는 `&kp C_MINIMIZE`를 지정해 누르는 동안 유지하거나, `&WINMINIMIZE`를 지정해 40ms 눌렀다 해제하는 한 번의 입력을 보낼 수 있습니다. WINMINIMIZE는 기존 이름과 편집기 호환성을 위해 ZMK의 매크로 비헤이비어로 감쌌지만, 단축키 조합이나 문자열 입력은 없습니다.

□ PC에 출력하는 중앙 장치에서 FULL Consumer Report를 명시하여 0x0206을 16비트로 전송합니다. USB와 BLE 모두 같은 HID Consumer Usage를 사용합니다.

□ USB HID 표준에 명령이 정의돼 있다는 사실과 Windows가 창 최소화로 처리한다는 사실은 다릅니다. 빌드 통과만으로 Windows 동작을 확인했다고 판단하지 않습니다. 실제 PC에서 적용 후 확인해야 합니다. Windows가 무시하면 이 Usage만으로 창을 최소화할 수 없습니다.

□ 최초 확인은 저장하지 않은 작업이 없는 일반 창에서 USB 출력으로 X+C를 한 번 누릅니다. 정상 크기와 최대화 상태를 각각 확인합니다. 같은 명령을 BLE 출력에서도 확인합니다. 기존 단축키를 별도로 보내는 자동 대체 동작은 없습니다.

□ 설정 초기화 펌웨어는 필요하지 않습니다. 기존에 FULL 보고서를 사용하던 경우 보고서 형식도 유지됩니다. 이전 펌웨어가 BASIC이었다면 BLE 호스트가 이전 HID 형식을 캐시할 수 있으므로 먼저 USB에서 확인하고, 필요할 때만 PC 페어링을 갱신합니다.

참고: [USB HID Usage Tables, AC Minimize](https://usb.org/sites/default/files/hut1_5.pdf#page=132), [ZMK HID 보고서 구현](https://github.com/zmkfirmware/zmk/blob/v0.3.0/app/include/zmk/hid.h).
