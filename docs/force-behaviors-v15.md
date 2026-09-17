# v15 Keymap Editor 명령

v14에는 펌웨어의 force_cfg 드라이버와 ZMK Studio 메타데이터가 들어 있었지만, Nick Coutsos Keymap Editor에서 읽는 keymap 정의를 빠뜨렸습니다. 이 편집기는 외부 모듈의 C 메타데이터를 읽지 않고, 알 수 없는 behavior의 두 매개변수도 편집하지 못합니다.

v15는 keymap 안에 매개변수 없는 여섯 명령을 정의합니다. Behaviors 탭의 Custom Behaviors 목록과 키 할당의 behavior 선택 목록에서 사용할 수 있습니다.

| 키 | 선택할 behavior | 동작 |
| --- | --- | --- |
| SYS Y | flock_up | 이동 억제 +100 |
| SYS H | flock_dn | 이동 억제 −100 |
| SYS U | fclick_up | 클릭 +100 |
| SYS J | fclick_dn | 클릭 −100 |
| SYS I | frel_up | 해제 +100 |
| SYS K | frel_dn | 해제 −100 |

각 키에는 해당 behavior만 지정합니다. 별도 숫자 매개변수를 입력하지 않습니다. 각 명령의 amount 속성은 keymap에서 지정하며 기본 100입니다. 범위는 1~2000입니다.

Studio에서 직접 명령과 조절량을 정하려면 기존 '포스터치 기준' force_cfg를 사용합니다. 코드에서는 예를 들어 &force_cfg FORCE_CLICK_UP 100을 사용할 수 있습니다. Keymap Editor에서는 위 여섯 명령을 사용합니다.

양쪽 펌웨어가 필요합니다. 이전 Studio 저장 키맵이 있으면 여섯 자리에 새 behavior를 지정해야 할 수 있습니다. v14에서 저장한 센서 기준값은 유지합니다. 센서 판정·전송·LCD에는 변경이 없습니다.

원리 확인에 사용한 편집기 문서: https://github.com/nickcoutsos/keymap-editor/wiki/Features#behavior-editing
