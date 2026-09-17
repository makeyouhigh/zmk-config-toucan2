현재 펌웨어: [v12 — 빠른 포스터치 재클릭·세 손가락 탭 보완](FIRMWARE_VERSIONS.md). 오른쪽 적용 대상이며, 실기기 성공률은 적용 후 확인이 필요합니다.

# ZMK config for beekeeb Toucan2 Keyboard

[The beekeeb Toucan2 Keyboard](https://beekeeb.com/introducing-toucan2/) is a wireless split 42-key column‑stagger keyboard that a display and a trackpad, with an aggressive stagger on the pinky columns.

# Customizations

- **Keymap**: [config/toucan.keymap](config/toucan.keymap)
- **General configs**: [boards/shields/toucan/toucan_left.conf](boards/shields/toucan/toucan_left.conf) and [boards/shields/toucan/toucan_right.conf](boards/shields/toucan/toucan_right.conf)
- **Swipe shortcuts**: the `swipe_button_mapper` node in [boards/shields/toucan/toucan.dtsi](boards/shields/toucan/toucan.dtsi)
- **Invert scroll / trackpad settings**: the `tps43_trackpad` node in [boards/shields/toucan/toucan_right.overlay](boards/shields/toucan/toucan_right.overlay)

# License

The code in this repo is available under the MIT license.

The included shield nice_view_gem is modified from https://github.com/M165437/nice-view-gem licensed under the MIT License.

The linked trackpad module is based on https://github.com/geeksville/zmk_driver_azoteq

ZMK code snippets are taken from the ZMK documentation under the MIT license.

The embedded font QuinqueFive is designed by GGBotNet, licensed under under the SIL Open Font License, Version 1.1.


1. 화면 변경
기존 가운데 타이핑 속도 점그래프를 활성 모디키 아이콘으로 교체합니다.
표시 순서는 Ctrl, Shift, Alt, Win입니다.
Ctrl은 꺾쇠, Shift는 위쪽 화살표, Alt는 옵션 기호 모양, Win은 네 칸 창 모양입니다.
누른 모디키만 중앙 정렬해 표시하며, 모두 떼면 해당 영역이 비워집니다.
좌우 Ctrl/Shift/Alt/Win은 각각 같은 종류의 아이콘으로 묶습니다.
두 개 이상을 누르면 해당 아이콘을 함께 표시합니다.
기존 배터리, 레이어, 연결/프로필 표시 위치와 수면 화면은 유지합니다.

기존 파일 교체:
boards/shields/nice_view_gem/CMakeLists.txt
boards/shields/nice_view_gem/Kconfig.defconfig
boards/shields/nice_view_gem/nice_view_gem.conf
boards/shields/nice_view_gem/widgets/screen.c
boards/shields/nice_view_gem/widgets/util.h

새 파일 추가:
boards/shields/nice_view_gem/widgets/modifiers.c
boards/shields/nice_view_gem/widgets/modifiers.h
