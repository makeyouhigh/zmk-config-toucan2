# v16 클릭 조절 명령 복구

v15의 클릭 기준 조절 키 U/J는 편집기와 양쪽 펌웨어에 등록되어 있었지만 오른쪽까지 전달되지 않았다. 오른쪽 LEVELS 조회에서 H/K를 통한 억제·해제 변경은 확인됐으나, J를 5회 눌러도 클릭은 4500이었다. 이후 사용자의 추가 조절 뒤 값은 억제 3550, 클릭 4500, 해제 3000, 이동 중 억제 4050, 클릭 5000이다.

ZMK v0.3의 `ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN`은 9이며 종료 문자까지 포함한다. 중앙 장치가 `strlcpy`로 전달할 때 실제 이름은 8글자까지만 남는다. v15의 `fclick_up`과 `fclick_dn`은 각각 `fclick_u`와 `fclick_d`가 되고, `force_cfg`는 `force_cf`가 되어 오른쪽의 등록 이름과 일치하지 않는다. 등록 여부만 확인했던 v15 검증은 이 전송 길이를 놓쳤다.

v16은 실제 devicetree 노드 이름만 `fclk_up`, `fclk_dn`, `fcfg`로 줄인다. keymap 참조 라벨과 사용자 표시 이름은 유지하므로 SYS Y/H·U/J·I/K 배치와 Custom Behaviors 여섯 항목은 유지된다. 두 드라이버에서 `sizeof(DEVICE_DT_NAME(...))`를 실제 ZMK 전송 상수와 비교하여 종료 문자까지 컴파일 시 검증한다. 패킷 형식이나 BLE 마우스 전송 경로는 변경하지 않는다.

저장된 센서 기준과 Bluetooth 연결 정보는 유지한다. 진단 프로토콜은 v16,2이며 데이터 형식은 v14/v15와 같다. 적용 후 패드에서 손을 떼고 SYS J를 5회 누르면 현재 클릭 4500은 4000, 이동 중 클릭 5000은 4500이어야 한다. H/K는 다시 누르지 않는다. 실제 값을 다시 읽어 확인해야 하며 자동 변경으로 간주하지 않는다.

ZMK Studio에 별도로 저장한 U/J 또는 force_cfg 바인딩은 실제 기기 이름 변경으로 재지정해야 할 수 있다. 저장된 키맵이나 페어링을 일괄 초기화하지 않는다. 파일에 정의된 기본 키맵은 기존 라벨을 그대로 사용한다.

원인 확인 소스:

1. [ZMK v0.3 전송 구조](https://github.com/zmkfirmware/zmk/blob/v0.3/app/include/zmk/split/bluetooth/service.h)
2. [중앙 장치 이름 복사](https://github.com/zmkfirmware/zmk/blob/v0.3/app/src/split/bluetooth/central.c)
3. [오른쪽 명령 수신](https://github.com/zmkfirmware/zmk/blob/v0.3/app/src/split/bluetooth/service.c)
