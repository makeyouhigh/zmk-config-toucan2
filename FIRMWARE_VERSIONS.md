# Toucan2 펌웨어 버전

현재 배포본: v8 — 코드 정리 + 250ms 홀드 드래그 복구 (2026-09-17)

| 버전 | 변경 내용 | 실제 펌웨어 소스 | 검증된 빌드 |
| --- | --- | --- | --- |
| v8 | 마지막에 추가한 오른쪽 전송 큐·제한 및 상시 진단 코드 제거, 250ms 정지 후 이동하는 홀드 드래그 복구 | [a59e42f](https://github.com/makeyouhigh/zmk-config-toucan2/commit/a59e42f0759ac81b1ff10183309b2618c8001832) | [35175784709](https://github.com/makeyouhigh/zmk-config-toucan2/actions/runs/35175784709) |

v8은 앞서 전달한 cleanup-hold250 파일에 배포 번호를 부여한 것입니다. 파일 내용은 동일하므로 버전 표시 때문에 다시 설치할 필요는 없습니다.

| 구분 | 버전 파일명 | 이전에 전달한 동일 파일명 | SHA256 |
| --- | --- | --- | --- |
| 왼쪽 | toucan_left-v8.uf2 | toucan_left-cleanup-hold250.uf2 | 605bc22f33a4c4a609b6b3ea7905f8df1c992f68d6f444c366d34e6353374b0f |
| 오른쪽 | toucan_right-v8.uf2 | toucan_right-cleanup-hold250.uf2 | ab951d7c967981fcc9ce17cb614657c5d7083217d8dac739b2b537ff07135916 |

기존 빌드 ZIP 내부의 파일명에는 버전이 없습니다. 위 빌드 번호와 SHA256으로 v8에 대응하는 파일을 확인할 수 있습니다.

코드 검사와 양쪽 빌드는 통과했습니다. 실제 장치에서 기존 BLE 지연·더블클릭·가운데 클릭 문제가 해결됐다는 의미는 아닙니다.

앞으로 사용자에게 다른 펌웨어를 전달할 때는 v9, v10 순으로 번호를 올립니다. 커밋 제목은 [v번호] 변경 내용, 파일명은 toucan_left-v번호.uf2 및 toucan_right-v번호.uf2로 통일합니다. 각 버전에 소스 커밋, 빌드 링크, 양쪽 적용 필요 여부와 확인된 검증 범위를 기록합니다.
